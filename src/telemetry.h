#ifndef MACPULSE_TELEMETRY_H
#define MACPULSE_TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#define MAX_CORES 128
#define HISTORY_LEN 120
#define MAX_PROCESSES 1024

typedef struct {
    pid_t pid;
    uid_t uid;
    char username[32];
    float cpu_pct;
    float gpu_pct;
    uint64_t mem_bytes;
    float mem_pct;
    char name[64];
    char state[8];
    uint64_t cpu_time_sec;
} ProcessEntry;

typedef struct {
    ProcessEntry items[MAX_PROCESSES];
    int count;
    int filtered_indices[MAX_PROCESSES];
    int filtered_count;
} ProcessTable;

typedef struct {
    // Hardware & OS Specs
    char chip_name[64];
    char model_name[64];
    char hostname[64];
    char os_version[64];
    int total_cores;
    int p_cores;
    int e_cores;
    int gpu_cores;
    double load_avg[3];
    char uptime_str[64];

    // CPU Telemetry
    float cpu_total_pct;
    float cpu_user_pct;
    float cpu_sys_pct;
    float cpu_idle_pct;
    float core_pct[MAX_CORES];
    float history_cpu[HISTORY_LEN];
    int history_cpu_count;

    // GPU Telemetry
    float gpu_device_pct;
    float gpu_renderer_pct;
    float gpu_tiler_pct;
    uint64_t gpu_in_use_bytes;
    uint64_t gpu_alloc_bytes;
    float history_gpu[HISTORY_LEN];
    int history_gpu_count;

    // Thermal & Sensors
    float cpu_temp_avg;
    float cpu_temp_max;
    float gpu_temp_avg;
    float gpu_temp_max;
    float soc_temp_max;
    float battery_temp;
    float ssd_temp;
    int thermal_state; // 0=Nominal, 1=Fair, 2=Serious, 3=Critical
    char thermal_state_str[16];

    // Unified Memory Telemetry
    uint64_t mem_total_bytes;
    uint64_t mem_used_bytes;
    uint64_t mem_free_bytes;
    uint64_t mem_active_bytes;
    uint64_t mem_inactive_bytes;
    uint64_t mem_wired_bytes;
    uint64_t mem_compressed_bytes;
    uint64_t swap_total_bytes;
    uint64_t swap_used_bytes;
    float mem_used_pct;

    // Network Telemetry
    uint64_t net_download_bps;
    uint64_t net_upload_bps;
    uint64_t net_total_in_bytes;
    uint64_t net_total_out_bytes;
    float history_net_down[HISTORY_LEN];
    float history_net_up[HISTORY_LEN];
    int history_net_count;

    // Disk Telemetry
    uint64_t disk_total_bytes;
    uint64_t disk_used_bytes;
    uint64_t disk_free_bytes;
    uint64_t disk_read_bps;
    uint64_t disk_write_bps;
    uint64_t disk_total_read_bytes;
    uint64_t disk_total_write_bytes;

    // Battery Telemetry
    bool has_battery;
    int battery_pct;
    bool battery_charging;
    char battery_source[32];

    // Process Table
    ProcessTable procs;
} SystemTelemetry;

void telemetry_init(void);
void telemetry_update(void);
const SystemTelemetry *telemetry_get(void);
void telemetry_filter_processes(const char *query);
void telemetry_sort_processes(int sort_mode, bool reverse);

// Sort modes
#define SORT_BY_CPU  0
#define SORT_BY_MEM  1
#define SORT_BY_PID  2
#define SORT_BY_NAME 3

#endif // MACPULSE_TELEMETRY_H
