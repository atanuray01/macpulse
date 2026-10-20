#include "telemetry.h"
#import <Foundation/Foundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <ifaddrs.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <net/if.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

// Dynamic IOHID function signatures for thermal sensors
typedef void *IOHIDEventSystemClientRef;
typedef void *IOHIDServiceClientRef;
typedef void *IOHIDEventRef;
typedef IOHIDEventSystemClientRef (*IOHIDEventSystemClientCreateFn)(
    CFAllocatorRef);
typedef void (*IOHIDEventSystemClientSetMatchingFn)(IOHIDEventSystemClientRef,
                                                    CFDictionaryRef);
typedef CFArrayRef (*IOHIDEventSystemClientCopyServicesFn)(
    IOHIDEventSystemClientRef);
typedef CFTypeRef (*IOHIDServiceClientCopyPropertyFn)(IOHIDServiceClientRef,
                                                      CFStringRef);
typedef IOHIDEventRef (*IOHIDServiceClientCopyEventFn)(IOHIDServiceClientRef,
                                                       int64_t, int32_t,
                                                       int64_t);
typedef double (*IOHIDEventGetFloatValueFn)(IOHIDEventRef, int32_t);

static IOHIDEventSystemClientRef s_hid_client = NULL;
static IOHIDServiceClientCopyPropertyFn s_copy_prop = NULL;
static IOHIDServiceClientCopyEventFn s_copy_event = NULL;
static IOHIDEventGetFloatValueFn s_get_float = NULL;

// AppleSMC kernel struct definitions for fallback hardware queries (Intel &
// dGPUs)
#define KERNEL_INDEX_SMC 2

typedef struct {
  char major;
  char minor;
  char build;
  char reserved[1];
  UInt16 release;
} SMCKeyData_vers_t;

typedef struct {
  UInt16 version;
  UInt16 length;
  UInt32 cpuPLimit;
  UInt32 gpuPLimit;
  UInt32 memPLimit;
} SMCKeyData_pLimitData_t;

typedef struct {
  UInt32 key;
  UInt8 data_type[4];
  UInt8 bytes[32];
  UInt32 data_size;
} SMCKeyData_keyInfo_t;

typedef struct {
  UInt32 key;
  SMCKeyData_vers_t vers;
  SMCKeyData_pLimitData_t pLimitData;
  SMCKeyData_keyInfo_t keyInfo;
  UInt8 result;
  UInt8 status;
  UInt8 data8;
  UInt32 data32;
  UInt8 bytes[32];
} SMCKeyData_t;

static uint32_t str_to_smc_key(const char *str) {
  uint32_t key = 0;
  for (int i = 0; i < 4 && str[i]; i++) {
    key = (key << 8) | (uint8_t)str[i];
  }
  return key;
}

static float read_smc_temp(io_connect_t conn, const char *key_str) {
  SMCKeyData_t input;
  SMCKeyData_t output;
  memset(&input, 0, sizeof(input));
  memset(&output, 0, sizeof(output));

  input.key = str_to_smc_key(key_str);
  input.data8 = 9; // SMC_CMD_READ_KEYINFO

  size_t in_size = sizeof(SMCKeyData_t);
  size_t out_size = sizeof(SMCKeyData_t);

  kern_return_t kr = IOConnectCallStructMethod(conn, KERNEL_INDEX_SMC, &input,
                                               in_size, &output, &out_size);
  if (kr != KERN_SUCCESS)
    return -1.0f;

  UInt32 data_size = output.keyInfo.data_size;
  char type[5] = {0};
  memcpy(type, output.keyInfo.data_type, 4);

  memset(&input, 0, sizeof(input));
  memset(&output, 0, sizeof(output));
  input.key = str_to_smc_key(key_str);
  input.keyInfo.data_size = data_size;
  input.data8 = 5; // SMC_CMD_READ_BYTES

  kr = IOConnectCallStructMethod(conn, KERNEL_INDEX_SMC, &input, in_size,
                                 &output, &out_size);
  if (kr != KERN_SUCCESS)
    return -1.0f;

  if (strcmp(type, "sp78") == 0 && data_size == 2) {
    int16_t raw = (output.bytes[0] << 8) | output.bytes[1];
    return (float)raw / 256.0f;
  }
  if (strcmp(type, "fpe2") == 0 && data_size == 2) {
    uint16_t raw = (output.bytes[0] << 8) | output.bytes[1];
    return (float)raw / 4.0f;
  }
  if (strcmp(type, "flt ") == 0 && data_size == 4) {
    float val = 0;
    memcpy(&val, output.bytes, 4);
    return val;
  }
  return -1.0f;
}

static float query_smc_sensor_group(const char *keys[], int key_count,
                                    float *max_out) {
  io_service_t service = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching("AppleSMC"));
  if (!service)
    return 0.0f;

  io_connect_t conn;
  if (IOServiceOpen(service, mach_task_self(), 0, &conn) != KERN_SUCCESS) {
    IOObjectRelease(service);
    return 0.0f;
  }

  float sum = 0.0f;
  float max_temp = 0.0f;
  int count = 0;

  for (int i = 0; i < key_count; i++) {
    float val = read_smc_temp(conn, keys[i]);
    if (val > 15.0f && val < 120.0f) {
      sum += val;
      if (val > max_temp)
        max_temp = val;
      count++;
    }
  }

  IOServiceClose(conn);
  IOObjectRelease(service);

  if (max_out && count > 0)
    *max_out = max_temp;
  return (count > 0) ? (sum / count) : 0.0f;
}

static SystemTelemetry s_telemetry;
static struct timeval s_prev_time;
static processor_cpu_load_info_t s_prev_cpu_load = NULL;
static natural_t s_prev_cpu_count = 0;
static mach_msg_type_number_t s_prev_cpu_info_count = 0;

static uint64_t s_prev_net_in = 0;
static uint64_t s_prev_net_out = 0;
static bool s_has_prev_net = false;

static uint64_t s_prev_disk_r = 0;
static uint64_t s_prev_disk_w = 0;
static bool s_has_prev_disk = false;

// Process CPU caching
typedef struct {
  pid_t pid;
  uint64_t total_time; // user + sys in nanoseconds
  uint64_t tick;
} ProcTimeCache;

#define MAX_PROC_CACHE 4096
static ProcTimeCache s_proc_cache[MAX_PROC_CACHE];
static uint64_t s_current_tick = 0;

static void init_thermal_sensors(void) {
  void *handle =
      dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_NOW);
  if (!handle)
    return;

  IOHIDEventSystemClientCreateFn clientCreate =
      (IOHIDEventSystemClientCreateFn)dlsym(handle,
                                            "IOHIDEventSystemClientCreate");
  IOHIDEventSystemClientSetMatchingFn setMatching =
      (IOHIDEventSystemClientSetMatchingFn)dlsym(
          handle, "IOHIDEventSystemClientSetMatching");
  s_copy_prop = (IOHIDServiceClientCopyPropertyFn)dlsym(
      handle, "IOHIDServiceClientCopyProperty");
  s_copy_event = (IOHIDServiceClientCopyEventFn)dlsym(
      handle, "IOHIDServiceClientCopyEvent");
  s_get_float =
      (IOHIDEventGetFloatValueFn)dlsym(handle, "IOHIDEventGetFloatValue");

  if (!clientCreate || !setMatching || !s_copy_prop || !s_copy_event ||
      !s_get_float)
    return;

  s_hid_client = clientCreate(kCFAllocatorDefault);
  if (!s_hid_client)
    return;

  int page = 0xff00;
  int usage = 5;
  CFNumberRef pageNum =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &page);
  CFNumberRef usageNum =
      CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &usage);
  const void *keys[] = {CFSTR("PrimaryUsagePage"), CFSTR("PrimaryUsage")};
  const void *vals[] = {pageNum, usageNum};
  CFDictionaryRef matchDict = CFDictionaryCreate(
      kCFAllocatorDefault, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  setMatching(s_hid_client, matchDict);
  CFRelease(matchDict);
  CFRelease(pageNum);
  CFRelease(usageNum);
}

static void detect_gpu_hardware(void) {
  s_telemetry.gpu_cores = 8; // Sensible default
  s_telemetry.has_discrete_gpu = false;
  s_telemetry.gpu_name[0] = '\0';
  s_telemetry.discrete_gpu_name[0] = '\0';
  s_telemetry.discrete_gpu_temp = 0.0f;

  // 1. Detect Metal device(s)
  @autoreleasepool {
    NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
    for (id<MTLDevice> dev in devices) {
      NSString *dev_name = [dev name];
      if (s_telemetry.gpu_name[0] == '\0') {
        strncpy(s_telemetry.gpu_name, [dev_name UTF8String],
                sizeof(s_telemetry.gpu_name) - 1);
      }
      if (![dev hasUnifiedMemory]) {
        s_telemetry.has_discrete_gpu = true;
        strncpy(s_telemetry.discrete_gpu_name, [dev_name UTF8String],
                sizeof(s_telemetry.discrete_gpu_name) - 1);
      }
    }
  }
  if (s_telemetry.gpu_name[0] == '\0') {
    strncpy(s_telemetry.gpu_name, "Apple Integrated GPU",
            sizeof(s_telemetry.gpu_name) - 1);
  }

  // 2. Detect IOAccelerator services & cores
  CFMutableDictionaryRef matching = IOServiceMatching("IOAccelerator");
  if (!matching)
    return;

  io_iterator_t iter;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) ==
      KERN_SUCCESS) {
    io_service_t service;
    while ((service = IOIteratorNext(iter)) != 0) {
      io_name_t svc_name;
      if (IORegistryEntryGetName(service, svc_name) == KERN_SUCCESS) {
        if (strcasestr(svc_name, "Radeon") || strcasestr(svc_name, "AMD") ||
            strcasestr(svc_name, "NVDA") || strcasestr(svc_name, "GeForce")) {
          s_telemetry.has_discrete_gpu = true;
          if (s_telemetry.discrete_gpu_name[0] == '\0') {
            strncpy(s_telemetry.discrete_gpu_name, svc_name,
                    sizeof(s_telemetry.discrete_gpu_name) - 1);
          }
        }
      }

      CFMutableDictionaryRef props = NULL;
      if (IORegistryEntryCreateCFProperties(
              service, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS &&
          props) {
        CFTypeRef coreCount =
            CFDictionaryGetValue(props, CFSTR("gpu-core-count"));
        if (!coreCount)
          coreCount = CFDictionaryGetValue(props, CFSTR("num-cores"));
        if (!coreCount)
          coreCount = CFDictionaryGetValue(props, CFSTR("gpu_core_count"));
        if (coreCount && CFGetTypeID(coreCount) == CFNumberGetTypeID()) {
          int cores = 0;
          CFNumberGetValue((CFNumberRef)coreCount, kCFNumberIntType, &cores);
          if (cores > 0)
            s_telemetry.gpu_cores = cores;
        }
        CFRelease(props);
      }
      IOObjectRelease(service);
    }
    IOObjectRelease(iter);
  }
}

void telemetry_init(void) {
  memset(&s_telemetry, 0, sizeof(s_telemetry));
  gettimeofday(&s_prev_time, NULL);

  // Static hardware info
  size_t sz = sizeof(s_telemetry.chip_name);
  if (sysctlbyname("machdep.cpu.brand_string", s_telemetry.chip_name, &sz, NULL,
                   0) != 0) {
    strncpy(s_telemetry.chip_name, "Apple Silicon",
            sizeof(s_telemetry.chip_name) - 1);
  }

  sz = sizeof(s_telemetry.model_name);
  if (sysctlbyname("hw.model", s_telemetry.model_name, &sz, NULL, 0) != 0) {
    strncpy(s_telemetry.model_name, "Mac", sizeof(s_telemetry.model_name) - 1);
  }

  int ncpu = 0;
  sz = sizeof(ncpu);
  if (sysctlbyname("hw.ncpu", &ncpu, &sz, NULL, 0) == 0) {
    s_telemetry.total_cores = ncpu;
  }

  int pcores = 0, ecores = 0;
  sz = sizeof(pcores);
  sysctlbyname("hw.perflevel0.physicalcpu", &pcores, &sz, NULL, 0);
  sz = sizeof(ecores);
  sysctlbyname("hw.perflevel1.physicalcpu", &ecores, &sz, NULL, 0);
  s_telemetry.p_cores = pcores;
  s_telemetry.e_cores = ecores;

  detect_gpu_hardware();
  gethostname(s_telemetry.hostname, sizeof(s_telemetry.hostname) - 1);

  @autoreleasepool {
    NSString *osStr =
        [[NSProcessInfo processInfo] operatingSystemVersionString];
    if (osStr) {
      strncpy(s_telemetry.os_version, [osStr UTF8String],
              sizeof(s_telemetry.os_version) - 1);
    }
  }

  init_thermal_sensors();
  telemetry_update();
}

static void update_uptime_and_load(void) {
  getloadavg(s_telemetry.load_avg, 3);

  struct timeval boottime;
  size_t len = sizeof(boottime);
  if (sysctlbyname("kern.boottime", &boottime, &len, NULL, 0) == 0) {
    time_t now = time(NULL);
    time_t uptime_sec = now - boottime.tv_sec;
    int days = (int)(uptime_sec / 86400);
    int hours = (int)((uptime_sec % 86400) / 3600);
    int mins = (int)((uptime_sec % 3600) / 60);
    if (days > 0) {
      snprintf(s_telemetry.uptime_str, sizeof(s_telemetry.uptime_str),
               "%dd %dh %dm", days, hours, mins);
    } else {
      snprintf(s_telemetry.uptime_str, sizeof(s_telemetry.uptime_str),
               "%dh %dm", hours, mins);
    }
  } else {
    strncpy(s_telemetry.uptime_str, "N/A", sizeof(s_telemetry.uptime_str) - 1);
  }
}

static void update_cpu(double dt) {
  natural_t cpu_count = 0;
  processor_info_array_t info_array = NULL;
  mach_msg_type_number_t info_count = 0;

  kern_return_t kr =
      host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpu_count,
                          &info_array, &info_count);
  if (kr != KERN_SUCCESS)
    return;

  processor_cpu_load_info_t cpu_load = (processor_cpu_load_info_t)info_array;

  if (s_prev_cpu_load && s_prev_cpu_count == cpu_count) {
    uint64_t total_user = 0, total_sys = 0, total_idle = 0, total_nice = 0;
    int max_c = cpu_count < MAX_CORES ? cpu_count : MAX_CORES;

    for (int i = 0; i < max_c; i++) {
      uint32_t user = cpu_load[i].cpu_ticks[CPU_STATE_USER] -
                      s_prev_cpu_load[i].cpu_ticks[CPU_STATE_USER];
      uint32_t sys = cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM] -
                     s_prev_cpu_load[i].cpu_ticks[CPU_STATE_SYSTEM];
      uint32_t idle = cpu_load[i].cpu_ticks[CPU_STATE_IDLE] -
                      s_prev_cpu_load[i].cpu_ticks[CPU_STATE_IDLE];
      uint32_t nice = cpu_load[i].cpu_ticks[CPU_STATE_NICE] -
                      s_prev_cpu_load[i].cpu_ticks[CPU_STATE_NICE];

      uint32_t core_total = user + sys + idle + nice;
      if (core_total > 0) {
        s_telemetry.core_pct[i] =
            ((float)(user + sys + nice) / (float)core_total) * 100.0f;
      } else {
        s_telemetry.core_pct[i] = 0.0f;
      }

      total_user += user;
      total_sys += sys;
      total_idle += idle;
      total_nice += nice;
    }

    uint64_t sum = total_user + total_sys + total_idle + total_nice;
    if (sum > 0) {
      s_telemetry.cpu_user_pct = ((float)total_user / (float)sum) * 100.0f;
      s_telemetry.cpu_sys_pct = ((float)total_sys / (float)sum) * 100.0f;
      s_telemetry.cpu_idle_pct = ((float)total_idle / (float)sum) * 100.0f;
      s_telemetry.cpu_total_pct =
          s_telemetry.cpu_user_pct + s_telemetry.cpu_sys_pct;
    }

    // Update history
    if (s_telemetry.history_cpu_count < HISTORY_LEN) {
      s_telemetry.history_cpu[s_telemetry.history_cpu_count++] =
          s_telemetry.cpu_total_pct;
    } else {
      memmove(&s_telemetry.history_cpu[0], &s_telemetry.history_cpu[1],
              sizeof(float) * (HISTORY_LEN - 1));
      s_telemetry.history_cpu[HISTORY_LEN - 1] = s_telemetry.cpu_total_pct;
    }
  }

  if (s_prev_cpu_load) {
    vm_deallocate(mach_task_self_, (vm_address_t)s_prev_cpu_load,
                  s_prev_cpu_info_count * sizeof(int));
  }
  s_prev_cpu_load = cpu_load;
  s_prev_cpu_count = cpu_count;
  s_prev_cpu_info_count = info_count;
}

static void update_gpu(void) {
  CFMutableDictionaryRef matching = IOServiceMatching("IOAccelerator");
  if (!matching)
    return;

  io_iterator_t iterator;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iterator) !=
      KERN_SUCCESS)
    return;

  io_service_t service;
  while ((service = IOIteratorNext(iterator)) != 0) {
    CFMutableDictionaryRef props = NULL;
    if (IORegistryEntryCreateCFProperties(service, &props, kCFAllocatorDefault,
                                          0) == KERN_SUCCESS &&
        props) {
      CFTypeRef perfStats =
          CFDictionaryGetValue(props, CFSTR("PerformanceStatistics"));
      if (perfStats && CFGetTypeID(perfStats) == CFDictionaryGetTypeID()) {
        CFDictionaryRef dict = (CFDictionaryRef)perfStats;
        long long val = 0;

        CFNumberRef devNum =
            CFDictionaryGetValue(dict, CFSTR("Device Utilization %"));
        if (devNum) {
          CFNumberGetValue(devNum, kCFNumberLongLongType, &val);
          s_telemetry.gpu_device_pct = (float)val;
        }

        CFNumberRef rendNum =
            CFDictionaryGetValue(dict, CFSTR("Renderer Utilization %"));
        if (rendNum) {
          CFNumberGetValue(rendNum, kCFNumberLongLongType, &val);
          s_telemetry.gpu_renderer_pct = (float)val;
        }

        CFNumberRef tileNum =
            CFDictionaryGetValue(dict, CFSTR("Tiler Utilization %"));
        if (tileNum) {
          CFNumberGetValue(tileNum, kCFNumberLongLongType, &val);
          s_telemetry.gpu_tiler_pct = (float)val;
        }

        CFNumberRef inUseNum =
            CFDictionaryGetValue(dict, CFSTR("In use system memory"));
        if (inUseNum) {
          CFNumberGetValue(inUseNum, kCFNumberLongLongType, &val);
          s_telemetry.gpu_in_use_bytes = (uint64_t)val;
        }

        CFNumberRef allocNum =
            CFDictionaryGetValue(dict, CFSTR("Alloc system memory"));
        if (allocNum) {
          CFNumberGetValue(allocNum, kCFNumberLongLongType, &val);
          s_telemetry.gpu_alloc_bytes = (uint64_t)val;
        }

        // Temperature reading from IOAccelerator (discrete AMD/Nvidia GPU
        // drivers or eGPUs)
        CFNumberRef tempNum =
            CFDictionaryGetValue(dict, CFSTR("Temperature(C)"));
        if (!tempNum)
          tempNum = CFDictionaryGetValue(dict, CFSTR("Temperature"));
        if (!tempNum)
          tempNum = CFDictionaryGetValue(dict, CFSTR("GPU Core Temperature"));
        if (!tempNum)
          tempNum = CFDictionaryGetValue(dict, CFSTR("Sensor Temperature"));
        if (!tempNum)
          tempNum = CFDictionaryGetValue(dict, CFSTR("gpu-temperature"));
        if (tempNum && CFGetTypeID(tempNum) == CFNumberGetTypeID()) {
          long long t_val = 0;
          CFNumberGetValue(tempNum, kCFNumberLongLongType, &t_val);
          if (t_val > 10 && t_val < 125) {
            float fg = (float)t_val;
            s_telemetry.discrete_gpu_temp = fg;
            if (s_telemetry.gpu_temp_avg <= 0.0f) {
              s_telemetry.gpu_temp_avg = fg;
              s_telemetry.gpu_temp_max = fg;
            } else {
              if (fg > s_telemetry.gpu_temp_max)
                s_telemetry.gpu_temp_max = fg;
              s_telemetry.gpu_temp_avg = (s_telemetry.gpu_temp_avg + fg) / 2.0f;
            }
          }
        }
      }
      CFRelease(props);
    }
    IOObjectRelease(service);
  }
  IOObjectRelease(iterator);

  // Update history
  if (s_telemetry.history_gpu_count < HISTORY_LEN) {
    s_telemetry.history_gpu[s_telemetry.history_gpu_count++] =
        s_telemetry.gpu_device_pct;
  } else {
    memmove(&s_telemetry.history_gpu[0], &s_telemetry.history_gpu[1],
            sizeof(float) * (HISTORY_LEN - 1));
    s_telemetry.history_gpu[HISTORY_LEN - 1] = s_telemetry.gpu_device_pct;
  }
}

static void update_thermal(void) {
  float cpu_sum = 0, cpu_max = 0;
  int cpu_cnt = 0;
  float gpu_sum = 0, gpu_max = 0;
  int gpu_cnt = 0;
  float soc_max = 0;

  if (s_hid_client) {
    void *handle =
        dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_NOW);
    if (handle) {
      IOHIDEventSystemClientCopyServicesFn copyServices =
          (IOHIDEventSystemClientCopyServicesFn)dlsym(
              handle, "IOHIDEventSystemClientCopyServices");
      if (copyServices) {
        CFArrayRef services = copyServices(s_hid_client);
        if (services) {
          CFIndex count = CFArrayGetCount(services);
          for (CFIndex i = 0; i < count; i++) {
            IOHIDServiceClientRef s =
                (IOHIDServiceClientRef)CFArrayGetValueAtIndex(services, i);
            char name[128] = "";
            CFTypeRef nameProp = s_copy_prop(s, CFSTR("Product"));
            if (nameProp) {
              if (CFGetTypeID(nameProp) == CFStringGetTypeID()) {
                CFStringGetCString((CFStringRef)nameProp, name, sizeof(name),
                                   kCFStringEncodingUTF8);
              }
              CFRelease(nameProp);
            }

            IOHIDEventRef event = s_copy_event(s, 15, 0, 0); // 15 = temperature
            if (event) {
              double temp = s_get_float(event, 15 << 16);
              if (temp > 10.0 && temp < 115.0 && !strcasestr(name, "tcal") &&
                  !strcasestr(name, "cal")) {
                float ftemp = (float)temp;
                if (ftemp > soc_max)
                  soc_max = ftemp;

                bool is_gpu =
                    (strstr(name, "PMU2 tdie") || strstr(name, "PMU2 tdev") ||
                     strcasestr(name, "gpu") || strcasestr(name, "graphics") ||
                     strcasestr(name, "radeon") || strcasestr(name, "amd") ||
                     strcasestr(name, "nvidia") ||
                     strcasestr(name, "geforce") || strcasestr(name, "dgpu") ||
                     strcasestr(name, "egpu") || strstr(name, "TG0") ||
                     strstr(name, "TG1") || strstr(name, "TG2"));

                bool is_cpu =
                    (!is_gpu &&
                     (strstr(name, "PMU tdie") || strstr(name, "PMU tdev") ||
                      strcasestr(name, "cpu") || strstr(name, "TC0") ||
                      strstr(name, "TC1")));

                if (is_gpu) {
                  gpu_sum += ftemp;
                  if (ftemp > gpu_max)
                    gpu_max = ftemp;
                  gpu_cnt++;
                  if (strcasestr(name, "dgpu") || strcasestr(name, "egpu") ||
                      strcasestr(name, "radeon") ||
                      strcasestr(name, "nvidia") || strstr(name, "TG1")) {
                    s_telemetry.discrete_gpu_temp = ftemp;
                  }
                } else if (is_cpu) {
                  cpu_sum += ftemp;
                  if (ftemp > cpu_max)
                    cpu_max = ftemp;
                  cpu_cnt++;
                } else if (strcasestr(name, "battery")) {
                  s_telemetry.battery_temp = ftemp;
                } else if (strcasestr(name, "NAND") ||
                           strcasestr(name, "ssd")) {
                  s_telemetry.ssd_temp = ftemp;
                }
              }
              CFRelease(event);
            }
          }
          CFRelease(services);
        }
      }
    }
  }

  if (cpu_cnt > 0) {
    s_telemetry.cpu_temp_avg = cpu_sum / cpu_cnt;
    s_telemetry.cpu_temp_max = cpu_max > 0 ? cpu_max : s_telemetry.cpu_temp_avg;
  } else if (s_telemetry.cpu_temp_avg <= 0.0f) {
    // Fallback to AppleSMC for CPU on Intel/other Macs
    const char *cpu_keys[] = {"TC0D", "TC0P", "TC0E", "TC0F", "TC1C", "TC2C"};
    float smc_max = 0.0f;
    float smc_cpu = query_smc_sensor_group(cpu_keys, 6, &smc_max);
    if (smc_cpu > 0.0f) {
      s_telemetry.cpu_temp_avg = smc_cpu;
      s_telemetry.cpu_temp_max = smc_max;
      if (smc_max > soc_max)
        soc_max = smc_max;
    }
  }

  if (gpu_cnt > 0) {
    s_telemetry.gpu_temp_avg = gpu_sum / gpu_cnt;
    s_telemetry.gpu_temp_max = gpu_max > 0 ? gpu_max : s_telemetry.gpu_temp_avg;
  } else if (s_telemetry.gpu_temp_avg <= 0.0f) {
    // Fallback to AppleSMC for GPU on Intel/separate card Macs
    const char *gpu_keys[] = {"TG0D", "TG0P", "TG1D", "TG1P", "TG0T", "TG1T"};
    float smc_max = 0.0f;
    float smc_gpu = query_smc_sensor_group(gpu_keys, 6, &smc_max);
    if (smc_gpu > 0.0f) {
      s_telemetry.gpu_temp_avg = smc_gpu;
      s_telemetry.gpu_temp_max = smc_max;
      s_telemetry.discrete_gpu_temp = smc_gpu;
      if (smc_max > soc_max)
        soc_max = smc_max;
    }
  }

  if (soc_max > 0)
    s_telemetry.soc_temp_max = soc_max;

  @autoreleasepool {
    NSProcessInfoThermalState ts = [[NSProcessInfo processInfo] thermalState];
    s_telemetry.thermal_state = (int)ts;
    switch (ts) {
    case NSProcessInfoThermalStateNominal:
      strncpy(s_telemetry.thermal_state_str, "Nominal",
              sizeof(s_telemetry.thermal_state_str) - 1);
      break;
    case NSProcessInfoThermalStateFair:
      strncpy(s_telemetry.thermal_state_str, "Fair",
              sizeof(s_telemetry.thermal_state_str) - 1);
      break;
    case NSProcessInfoThermalStateSerious:
      strncpy(s_telemetry.thermal_state_str, "Serious",
              sizeof(s_telemetry.thermal_state_str) - 1);
      break;
    case NSProcessInfoThermalStateCritical:
      strncpy(s_telemetry.thermal_state_str, "Critical",
              sizeof(s_telemetry.thermal_state_str) - 1);
      break;
    default:
      strncpy(s_telemetry.thermal_state_str, "Unknown",
              sizeof(s_telemetry.thermal_state_str) - 1);
      break;
    }
  }
}

static void update_memory(void) {
  int64_t total_ram = 0;
  size_t sz = sizeof(total_ram);
  if (sysctlbyname("hw.memsize", &total_ram, &sz, NULL, 0) == 0) {
    s_telemetry.mem_total_bytes = (uint64_t)total_ram;
  }

  vm_size_t page_size = 0;
  mach_port_t mach_port = mach_host_self();
  host_page_size(mach_port, &page_size);

  vm_statistics64_data_t vm_stat;
  mach_msg_type_number_t count =
      sizeof(vm_statistics64_data_t) / sizeof(integer_t);
  if (host_statistics64(mach_port, HOST_VM_INFO64, (host_info64_t)&vm_stat,
                        &count) == KERN_SUCCESS) {
    s_telemetry.mem_active_bytes = (uint64_t)vm_stat.active_count * page_size;
    s_telemetry.mem_inactive_bytes =
        (uint64_t)vm_stat.inactive_count * page_size;
    s_telemetry.mem_wired_bytes = (uint64_t)vm_stat.wire_count * page_size;
    s_telemetry.mem_compressed_bytes =
        (uint64_t)vm_stat.compressor_page_count * page_size;
    s_telemetry.mem_free_bytes = (uint64_t)vm_stat.free_count * page_size;

    s_telemetry.mem_used_bytes = s_telemetry.mem_active_bytes +
                                 s_telemetry.mem_wired_bytes +
                                 s_telemetry.mem_compressed_bytes;
    if (s_telemetry.mem_total_bytes > 0) {
      s_telemetry.mem_used_pct = ((float)s_telemetry.mem_used_bytes /
                                  (float)s_telemetry.mem_total_bytes) *
                                 100.0f;
    }
  }

  struct xsw_usage swap;
  sz = sizeof(swap);
  if (sysctlbyname("vm.swapusage", &swap, &sz, NULL, 0) == 0) {
    s_telemetry.swap_total_bytes = swap.xsu_total;
    s_telemetry.swap_used_bytes = swap.xsu_used;
  }

  if (s_telemetry.history_mem_count < HISTORY_LEN) {
    s_telemetry.history_mem[s_telemetry.history_mem_count++] =
        s_telemetry.mem_used_pct;
  } else {
    memmove(&s_telemetry.history_mem[0], &s_telemetry.history_mem[1],
            sizeof(float) * (HISTORY_LEN - 1));
    s_telemetry.history_mem[HISTORY_LEN - 1] = s_telemetry.mem_used_pct;
  }
}

static void update_network(double dt) {
  struct ifaddrs *ifaddr;
  if (getifaddrs(&ifaddr) == -1)
    return;

  uint64_t current_in = 0, current_out = 0;
  for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr)
      continue;
    if (ifa->ifa_addr->sa_family == AF_LINK) {
      struct if_data *ifd = (struct if_data *)ifa->ifa_data;
      if (ifd && !(ifa->ifa_flags & IFF_LOOPBACK) &&
          (ifa->ifa_flags & IFF_UP)) {
        current_in += ifd->ifi_ibytes;
        current_out += ifd->ifi_obytes;
      }
    }
  }
  freeifaddrs(ifaddr);

  s_telemetry.net_total_in_bytes = current_in;
  s_telemetry.net_total_out_bytes = current_out;

  if (s_has_prev_net && dt > 0.05) {
    uint64_t diff_in =
        (current_in >= s_prev_net_in) ? (current_in - s_prev_net_in) : 0;
    uint64_t diff_out =
        (current_out >= s_prev_net_out) ? (current_out - s_prev_net_out) : 0;
    s_telemetry.net_download_bps = (uint64_t)((double)diff_in / dt);
    s_telemetry.net_upload_bps = (uint64_t)((double)diff_out / dt);
  } else {
    s_telemetry.net_download_bps = 0;
    s_telemetry.net_upload_bps = 0;
    s_has_prev_net = true;
  }

  s_prev_net_in = current_in;
  s_prev_net_out = current_out;

  // Update history (in KB/s for graph)
  float down_kb = (float)s_telemetry.net_download_bps / 1024.0f;
  float up_kb = (float)s_telemetry.net_upload_bps / 1024.0f;
  if (s_telemetry.history_net_count < HISTORY_LEN) {
    s_telemetry.history_net_down[s_telemetry.history_net_count] = down_kb;
    s_telemetry.history_net_up[s_telemetry.history_net_count] = up_kb;
    s_telemetry.history_net_count++;
  } else {
    memmove(&s_telemetry.history_net_down[0], &s_telemetry.history_net_down[1],
            sizeof(float) * (HISTORY_LEN - 1));
    memmove(&s_telemetry.history_net_up[0], &s_telemetry.history_net_up[1],
            sizeof(float) * (HISTORY_LEN - 1));
    s_telemetry.history_net_down[HISTORY_LEN - 1] = down_kb;
    s_telemetry.history_net_up[HISTORY_LEN - 1] = up_kb;
  }
}

static void update_disk(double dt) {
  // Disk space
  struct statfs sfs;
  if (statfs("/", &sfs) == 0) {
    uint64_t total = (uint64_t)sfs.f_blocks * sfs.f_bsize;
    uint64_t free = (uint64_t)sfs.f_bavail * sfs.f_bsize;
    s_telemetry.disk_total_bytes = total;
    s_telemetry.disk_free_bytes = free;
    s_telemetry.disk_used_bytes = (total >= free) ? (total - free) : 0;
  }

  // Disk I/O
  CFMutableDictionaryRef matching = IOServiceMatching("IOBlockStorageDriver");
  if (!matching)
    return;

  io_iterator_t iter;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, matching, &iter) ==
      KERN_SUCCESS) {
    io_service_t service;
    uint64_t total_r = 0, total_w = 0;
    while ((service = IOIteratorNext(iter)) != 0) {
      CFMutableDictionaryRef props = NULL;
      if (IORegistryEntryCreateCFProperties(
              service, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS &&
          props) {
        CFDictionaryRef stats =
            (CFDictionaryRef)CFDictionaryGetValue(props, CFSTR("Statistics"));
        if (stats && CFGetTypeID(stats) == CFDictionaryGetTypeID()) {
          long long r = 0, w = 0;
          CFNumberRef rNum = CFDictionaryGetValue(stats, CFSTR("Bytes (Read)"));
          CFNumberRef wNum =
              CFDictionaryGetValue(stats, CFSTR("Bytes (Write)"));
          if (rNum)
            CFNumberGetValue(rNum, kCFNumberLongLongType, &r);
          if (wNum)
            CFNumberGetValue(wNum, kCFNumberLongLongType, &w);
          total_r += r;
          total_w += w;
        }
        CFRelease(props);
      }
      IOObjectRelease(service);
    }
    IOObjectRelease(iter);

    s_telemetry.disk_total_read_bytes = total_r;
    s_telemetry.disk_total_write_bytes = total_w;

    if (s_has_prev_disk && dt > 0.05) {
      uint64_t diff_r =
          (total_r >= s_prev_disk_r) ? (total_r - s_prev_disk_r) : 0;
      uint64_t diff_w =
          (total_w >= s_prev_disk_w) ? (total_w - s_prev_disk_w) : 0;
      s_telemetry.disk_read_bps = (uint64_t)((double)diff_r / dt);
      s_telemetry.disk_write_bps = (uint64_t)((double)diff_w / dt);
    } else {
      s_telemetry.disk_read_bps = 0;
      s_telemetry.disk_write_bps = 0;
      s_has_prev_disk = true;
    }
    s_prev_disk_r = total_r;
    s_prev_disk_w = total_w;
  }
}

static void update_battery(void) {
  CFTypeRef info = IOPSCopyPowerSourcesInfo();
  if (!info) {
    s_telemetry.has_battery = false;
    return;
  }

  CFArrayRef list = IOPSCopyPowerSourcesList(info);
  if (list && CFArrayGetCount(list) > 0) {
    CFDictionaryRef dict =
        IOPSGetPowerSourceDescription(info, CFArrayGetValueAtIndex(list, 0));
    if (dict) {
      s_telemetry.has_battery = true;
      int curCap = 0, maxCap = 0;
      CFNumberRef cur =
          CFDictionaryGetValue(dict, CFSTR(kIOPSCurrentCapacityKey));
      CFNumberRef max = CFDictionaryGetValue(dict, CFSTR(kIOPSMaxCapacityKey));
      CFBooleanRef isCharging =
          CFDictionaryGetValue(dict, CFSTR(kIOPSIsChargingKey));
      CFStringRef state =
          CFDictionaryGetValue(dict, CFSTR(kIOPSPowerSourceStateKey));

      if (cur)
        CFNumberGetValue(cur, kCFNumberIntType, &curCap);
      if (max)
        CFNumberGetValue(max, kCFNumberIntType, &maxCap);
      s_telemetry.battery_pct = maxCap > 0 ? (curCap * 100 / maxCap) : 0;
      s_telemetry.battery_charging =
          (isCharging && CFBooleanGetValue(isCharging));

      if (state && CFGetTypeID(state) == CFStringGetTypeID()) {
        CFStringGetCString(state, s_telemetry.battery_source,
                           sizeof(s_telemetry.battery_source),
                           kCFStringEncodingUTF8);
      }
    }
    CFRelease(list);
  } else {
    s_telemetry.has_battery = false;
  }
  CFRelease(info);
}

static void update_processes(double dt) {
  s_current_tick++;
  int num_pids = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
  if (num_pids <= 0)
    return;

  pid_t *pids = malloc(sizeof(pid_t) * num_pids);
  if (!pids)
    return;

  int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pid_t) * num_pids);
  int count = bytes / sizeof(pid_t);

  int out_cnt = 0;
  for (int i = 0; i < count && out_cnt < MAX_PROCESSES; i++) {
    pid_t pid = pids[i];
    if (pid <= 0)
      continue;

    struct proc_bsdinfo bsd_info;
    if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bsd_info, sizeof(bsd_info)) <=
        0)
      continue;

    struct proc_taskinfo task_info;
    int has_task =
        proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &task_info, sizeof(task_info));

    ProcessEntry pe;
    memset(&pe, 0, sizeof(pe));
    pe.pid = pid;
    pe.uid = bsd_info.pbi_uid;
    strncpy(pe.name, bsd_info.pbi_name, sizeof(pe.name) - 1);

    struct passwd *pw = getpwuid(bsd_info.pbi_uid);
    if (pw) {
      strncpy(pe.username, pw->pw_name, sizeof(pe.username) - 1);
    } else {
      snprintf(pe.username, sizeof(pe.username), "%d", bsd_info.pbi_uid);
    }

    switch (bsd_info.pbi_status) {
    case 1:
      strncpy(pe.state, "IDLE", sizeof(pe.state) - 1);
      break;
    case 2:
      strncpy(pe.state, "RUN", sizeof(pe.state) - 1);
      break;
    case 3:
      strncpy(pe.state, "SLEEP", sizeof(pe.state) - 1);
      break;
    case 4:
      strncpy(pe.state, "STOP", sizeof(pe.state) - 1);
      break;
    case 5:
      strncpy(pe.state, "ZOMB", sizeof(pe.state) - 1);
      break;
    default:
      strncpy(pe.state, "WAIT", sizeof(pe.state) - 1);
      break;
    }

    if (has_task > 0) {
      pe.mem_bytes = task_info.pti_resident_size;
      if (s_telemetry.mem_total_bytes > 0) {
        pe.mem_pct =
            ((float)pe.mem_bytes / (float)s_telemetry.mem_total_bytes) * 100.0f;
      }

      uint64_t total_cpu_ns =
          task_info.pti_total_user + task_info.pti_total_system;
      pe.cpu_time_sec = total_cpu_ns / 1000000000ULL;

      // Lookup cache
      int cache_slot = pid % MAX_PROC_CACHE;
      if (s_proc_cache[cache_slot].pid == pid && dt > 0.05) {
        uint64_t diff =
            (total_cpu_ns >= s_proc_cache[cache_slot].total_time)
                ? (total_cpu_ns - s_proc_cache[cache_slot].total_time)
                : 0;
        float pct = ((float)diff / (float)(dt * 1000000000.0)) * 100.0f;
        pe.cpu_pct = pct;
      } else {
        pe.cpu_pct = 0.0f;
      }

      // GPU usage estimation for UI renderers / Metal clients
      if (strstr(pe.name, "WindowServer") || strstr(pe.name, "GPU") ||
          strstr(pe.name, "Terminal") || strstr(pe.name, "mactop") ||
          strstr(pe.name, "macpulse")) {
        pe.gpu_pct = pe.cpu_pct * 0.4f;
        if (pe.gpu_pct > s_telemetry.gpu_device_pct)
          pe.gpu_pct = s_telemetry.gpu_device_pct;
      }

      s_proc_cache[cache_slot].pid = pid;
      s_proc_cache[cache_slot].total_time = total_cpu_ns;
      s_proc_cache[cache_slot].tick = s_current_tick;
    }

    s_telemetry.procs.items[out_cnt++] = pe;
  }
  s_telemetry.procs.count = out_cnt;
  free(pids);

  telemetry_filter_processes("");
}

void telemetry_update(void) {
  struct timeval cur_time;
  gettimeofday(&cur_time, NULL);
  double dt = (cur_time.tv_sec - s_prev_time.tv_sec) +
              (cur_time.tv_usec - s_prev_time.tv_usec) / 1000000.0;
  s_prev_time = cur_time;

  update_uptime_and_load();
  update_cpu(dt);
  update_gpu();
  update_thermal();
  update_memory();
  update_network(dt);
  update_disk(dt);
  update_battery();
  update_processes(dt);
}

const SystemTelemetry *telemetry_get(void) { return &s_telemetry; }

static char s_filter_query[64] = "";
static int s_current_sort = SORT_BY_CPU;
static bool s_current_reverse = false;

static int proc_compare(const void *a, const void *b) {
  int idxA = *(const int *)a;
  int idxB = *(const int *)b;
  const ProcessEntry *pA = &s_telemetry.procs.items[idxA];
  const ProcessEntry *pB = &s_telemetry.procs.items[idxB];

  int res = 0;
  switch (s_current_sort) {
  case SORT_BY_CPU:
    if (pA->cpu_pct > pB->cpu_pct)
      res = -1;
    else if (pA->cpu_pct < pB->cpu_pct)
      res = 1;
    break;
  case SORT_BY_MEM:
    if (pA->mem_bytes > pB->mem_bytes)
      res = -1;
    else if (pA->mem_bytes < pB->mem_bytes)
      res = 1;
    break;
  case SORT_BY_PID:
    res = (pA->pid < pB->pid) ? -1 : 1;
    break;
  case SORT_BY_NAME:
    res = strcasecmp(pA->name, pB->name);
    break;
  }
  return s_current_reverse ? -res : res;
}

void telemetry_filter_processes(const char *query) {
  if (query) {
    strncpy(s_filter_query, query, sizeof(s_filter_query) - 1);
  }
  int match_cnt = 0;
  for (int i = 0; i < s_telemetry.procs.count; i++) {
    if (s_filter_query[0] == '\0' ||
        strcasestr(s_telemetry.procs.items[i].name, s_filter_query) ||
        strcasestr(s_telemetry.procs.items[i].username, s_filter_query)) {
      s_telemetry.procs.filtered_indices[match_cnt++] = i;
    }
  }
  s_telemetry.procs.filtered_count = match_cnt;

  // Sort matching indices
  if (match_cnt > 1) {
    qsort(s_telemetry.procs.filtered_indices, match_cnt, sizeof(int),
          proc_compare);
  }
}

void telemetry_sort_processes(int sort_mode, bool reverse) {
  s_current_sort = sort_mode;
  s_current_reverse = reverse;
  telemetry_filter_processes(s_filter_query);
}
