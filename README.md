# MacPulse ◈

> **Ultra-Fast, Real-Time Apple Silicon & Mac System Monitor & Dashboard for Terminal**
> *Sudo-free native telemetry, rolling graphs, unified memory breakdown, interactive activity monitor, 10 full-screen layout presets & 15 pro color themes.*

---

```
  __  __            _____      _           
 |  \/  |          |  __ \    | |          
 | \  / | __ _  ___| |__) |  _| |___  ___  
 | |\/| |/ _` |/ __|  ___/ | | | / __|/ _ \ 
 | |  | | (_| | (__| |   | |_| | \__ \  __/ 
 |_|  |_|\__,_|\___|_|    \__,_|_|___/\___| 
                                           
        ◈ Apple Silicon & Mac Terminal Dashboard ◈
```

---

## ⚡ Highlights & Comparison vs Mactop

| Feature | `mactop` | `btop` / `htop` | **`MacPulse` (This App)** |
| :--- | :---: | :---: | :---: |
| **No Sudo Required** | ✅ Yes | ❌ Needs Root for GPU | **✅ Yes (100% Native macOS APIs)** |
| **Apple Silicon (M1–M5) & Intel Macs** | ✅ Yes | ⚠️ Partial | **✅ Yes (P-Cores, E-Cores, SoC & Intel)** |
| **CPU Usage History Graph** | ❌ None | ⚠️ Basic | **✅ Multi-Level Unicode Sparklines (ttyplot)** |
| **GPU Utilization (Device, Renderer, Tiler)** | ⚠️ Basic | ❌ Missing on Mac | **✅ Full `IOAccelerator` Telemetry** |
| **Integrated & Discrete GPU Telemetry** | ⚠️ Integrated | ❌ None | **✅ Apple Silicon SoC + AMD/eGPU/PCIe Cards** |
| **GPU Temperatures** | ⚠️ Avg only | ❌ None | **✅ SoC Die (`PMU2`), dGPU IOKit, IOHID & SMC** |
| **GPU VRAM In-Use vs Allocated** | ❌ None | ❌ None | **✅ Live VRAM Breakdown** |
| **System Thermal Throttling State** | ❌ None | ❌ None | **✅ Nominal / Fair / Serious / Critical** |
| **Unified Memory Breakdown** | ⚠️ Basic | ⚠️ Standard RAM | **✅ App, Wired, Compressed, Cache, Swap** |
| **Network Speed & Disk I/O** | ❌ None | ⚠️ Basic | **✅ Live ▼ Down / ▲ Up + Read/Write Rates** |
| **Activity Monitor / Process Table** | ❌ None | ✅ Yes | **✅ Live %CPU, %MEM, %GPU, Search & Kill** |
| **Dashboard Layout Presets** | 1 View | 1 View | **✅ 10 Full-Screen Views (Full, Bento, Rail, CPU...)** |
| **Theme Customization** | ❌ Minimal | ⚠️ Fixed Palettes | **✅ 15 Pro Themes + Live Modal + JSON** |
| **Zero-Flicker Double Buffering** | ⚠️ Curses | ⚠️ Redraws | **✅ Atomic Double-Buffered Frames** |
| **Binary Footprint & CPU Usage** | ~15MB | ~25MB | **< 1MB binary, < 0.3% CPU usage** |

---

## 🖥️ Platform Limitation: macOS Only

> [!IMPORTANT]
> **MacPulse is strictly engineered for macOS.**
> It does **not** support Linux or Windows.

MacPulse achieves its high performance, zero-sudo monitoring, and sub-megabyte footprint by directly interfacing with low-level Apple Darwin and macOS frameworks:
- **Darwin Kernel & Mach Host APIs** (`host_processor_info`, `host_statistics64`, `proc_pidinfo`)
- **IOKit & IOAccelerator** (`PerformanceStatistics` for Apple Silicon GPU and discrete PCIe cards)
- **IOHIDEventSystemClient** (Direct hardware thermal die reading on Apple Silicon SoCs)
- **Metal Framework** (`MTLCopyAllDevices()` for discrete GPU card and unified memory detection)
- **AppleSMC** (Direct sensor queries for discrete Intel/AMD Mac graphics hardware)

Supporting Linux or Windows would require entirely separate telemetry engines (e.g., `/proc`, `sysfs`, `nvml`, `udev` on Linux; PDH, WMI, and DXGI on Windows). MacPulse intentionally focuses 100% on providing the best possible macOS and Apple Silicon experience.

---

## 📸 Dashboard Layout Presets (10 Views)

MacPulse offers **10 specialized full-screen dashboard layout presets** to suit different monitoring workflows. Bento Grid and Metrics Rail rearrange the header, metric panels, graphs, and process table into distinct screen compositions rather than only swapping the main content. Switch layouts with **`v`**, or launch with `-V`, `--view`, or `--layout`.

```
◈ MacPulse • Apple M4 • 10 Cores (6E+4P) • 8 GPU • v1.0.0            ⚡ 89% • Nominal • Up: 4d 2h • 16:50:00
┌─ CPU MONITOR • 34.0°C ─────┐┌─ GPU & VRAM ───────────────┐┌─ UNIFIED MEMORY ───────────┐┌─ NETWORK SPEED ────────────┐
│████░░░░░░░░░░░░░░░░░░░  18%││██░░░░░░░░░░░░░░░░░░░░░  12%││████████████░░░░░░░░░░░  53%││ Download: ▼  3.42 MB/s     │
│ User: 12%  Sys:  6%        ││ Renderer: 12%  Tiler:  4%  ││ App: 5.2GB  Wired: 1.8GB   ││ Upload:   ▲ 480.2 KB/s     │
│ Load: 1.64 1.52 1.48       ││ VRAM: 335.3MB / 1.3GB      ││ Comp: 1.6GB  Cache: 5.0GB  ││ In: 14.2GB  Out: 2.4GB     │
│ 10 Cores (6E + 4P)         ││ 8 GPU Cores • Metal 3      ││ Free: 1.9GB  Swap: 0B      ││ Disk IO: R 16KB/s W 4KB/s   │
└────────────────────────────┘└──────────────────────────────┘└──────────────────────────────┘└─────────────────────────────┘
┌─ CPU USAGE HISTORY • Apple Silicon Cores ────────────────┐┌─ GPU USAGE HISTORY • IOAccelerator ────────────────────┐
│ ^ 100 %                                                  ││ ^ 100 %                                                  │
│ |                                                        ││ |                                                        │
│ |  75 %    . . . . . . . . . . . . . . . . . . . . . . . ││ |  75 %    . . . . . . . . . . . . . . . . . . . . . . . │
│ |                                           █            ││ |                                                        │
│ |  50 %    . . . . . . . . . . . . . . . . ▄ █           ││ |  50 %    . . . . . . . . . . . . . . . . . . . . . . . │
│ |                                      ▄ █ █ █ █         ││ |                                           ▄ █          │
│ |  25 %    . . . . . . . . . . . . . ▄ █ █ █ █ █ █       ││ |  25 %    . . . . . . . . . . . . . . . . ▄ █ █ █       │
│ |                                ▄ █ █ █ █ █ █ █ █ █     ││ |                                      ▄ █ █ █ █ █ █     │
│ +------------------------------------------------------->││ +------------------------------------------------------->│
│ | last=18.4 min= 3.2 max=88.5 avg=21.6 %     ttyplot 1.0 ││ | last=12.0 min= 0.0 max=42.0 avg= 9.4 %     ttyplot 1.0 │
└──────────────────────────────────────────────────────────┘└──────────────────────────────────────────────────────────┘
┌─ ACTIVITY MONITOR (494 processes) • ↑/↓ scroll, / search, x kill, Tab toggle ────────────────────────────────────────┐
│ PID     USER          RES         %CPU   %GPU   %MEM  TIME     COMMAND                                               │
│ 38579   atanuray      678.1MB     14.2%  0.0%   4.1%  0m02s    Antigravity IDE Helper                                │
│ 11709   atanuray      1.63GB       8.5% 10.2%   10.2% 1m45s    com.apple.WebKit.WebContent                           │
│ 639     atanuray      67.2MB       1.2%  0.0%   0.4%  0m09s    ControlCenter                                         │
│ 645     atanuray      120.2MB      0.8%  0.0%   0.7%  0m04s    Finder                                                │
└──────────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘
 [q]Exit [t]Themes [v]Layout:Full [g]Graphs:Split [Tab]HideProcs [c]CPU [m]Mem [p]PID [/]Search [x]Kill [+/-]1.0s
```

### Available Views

1. **Full Dashboard (`full`)** *(Default)*:
   The complete command center: 4 stat cards (CPU, GPU, Memory, Network), rolling Unicode history graphs (CPU & GPU), and the full interactive Activity Monitor process table.
2. **Compact Layout (`compact`)**:
   Horizontal density layout with compact metric cards, a rolling history graph, and an expanded process table for smaller terminal windows.
3. **Zen Graphs (`zen`)**:
   Minimalist graphical mode that maximizes the full screen for Unicode history plots with a compact status ticker along the bottom.
4. **System Overview (`overview`)**:
   High-level health and hardware banner (chip model, cores, thermal state, battery health, uptime) alongside 4 metric cards and top resource-consuming processes.
5. **CPU-Focused (`cpu`)**:
   Deep-dive CPU view featuring per-core Efficiency (E) and Performance (P) core meters, detailed user/system/idle load breakdown, full-width CPU history graph, and auto-sorted CPU process list.
6. **GPU-Focused (`gpu`)**:
   Graphics engine view showing device utilization, renderer/tiler load, VRAM usage vs allocation, discrete GPU card detection, GPU die temperatures, full-width GPU graph, and GPU-sorted processes.
7. **Memory-Focused (`mem`)**:
   Unified memory deep-dive featuring a multi-segment proportional memory bar (App, Wired, Compressed, Cached, Free), Mach VM page table statistics, memory history graph, and memory-sorted process list.
8. **Network & Disk (`net`)**:
   I/O throughput center displaying real-time download/upload gauges, cumulative session traffic, disk read/write bandwidth, dual-series network speed history graph, and I/O-active processes.
9. **Bento Grid (`bento`)**:
   A two-row metric-card grid with centered CPU, GPU, and memory bars using the full available panel width; history graphs and the process table sit side-by-side below a custom two-line status header.
10. **Metrics Rail (`rail`)**:
    A vertical CPU/GPU/memory/network meter rail beside a separate graph-and-process workspace, reflowing the dashboard into a sidebar composition.

---

## 🌡️ GPU Telemetry & Temperature Support

### Integrated & Separate / Discrete GPU Architecture
MacPulse supports both Apple Silicon unified graphics and separate/discrete GPUs (eGPUs, AMD Radeon PCIe cards on Intel Macs or Mac Pro):

- **Apple Silicon Integrated GPUs**:
  Telemetry is extracted directly from the Apple Silicon `IOAccelerator` device. GPU die temperatures are queried via `IOHIDEventSystemClient` using Apple SoC thermal sensors (`PMU2 tdie`, `PMU2 tdev`).
- **Separate / Discrete GPUs (dGPU & eGPU)**:
  Detected via the Metal framework (`MTLCopyAllDevices()`) by inspecting devices without unified memory. Temperatures are extracted using a multi-tiered pipeline:
  1. `IOAccelerator` `PerformanceStatistics` dictionary keys (`Temperature(C)`, `Temperature`, `GPU Core Temperature`, `Sensor Temperature`).
  2. IOHID thermal sensors matched against discrete GPU signatures (`dGPU`, `eGPU`, `Radeon`, `AMD`, `NVIDIA`, `GeForce`).
  3. AppleSMC fallback querying discrete GPU temperature keys (`TG0D`, `TG0P`, `TG1D`, `TG1P`).

> [!NOTE]
> **GPU Metric Accuracy Note**: Total GPU Device %, Renderer %, and Tiler % are hardware measurements provided directly by the macOS `IOAccelerator` kernel driver.
> The per-process `%GPU` column in the Activity Monitor is an algorithmic estimate derived from process UI rendering activity, Metal execution contexts, and CPU work, as macOS Darwin APIs do not expose unprivileged per-process GPU hardware counters without root/DTrace permissions.

---

## 🎨 Themes (15 Built-in Themes + Custom JSON)

MacPulse includes **15 handcrafted 24-bit Truecolor RGB themes** (default: **Matrix Hacker**). Switch themes live inside the app using **`t`** (modal picker) or **`T`** (instant cycle).

All built-in and JSON themes color full-width single-row block meters, with the percentage label at the end of each panel's available meter row. Compact mode keeps one-row meters.

| # | Theme Name | Description |
| :---: | :--- | :--- |
| 1 | **Matrix Hacker** *(Default)* | Digital rain monochrome phosphor greens |
| 2 | **Linux Arch Cyan** | Electric cyan, deep navy, and ice blue Linux terminal |
| 3 | **Ubuntu Amber** | Classic warm phosphor amber and Ubuntu orange |
| 4 | **Cyberpunk 2077** | High contrast neon pink, electric yellow and laser cyan |
| 5 | **Dracula Pro** | Iconic gothic dark theme with vibrant pastels |
| 6 | **Tokyo Night** | Modern clean indigo, soft cyan, purple and warm orange |
| 7 | **Nord Arctic** | Arctic blue, aurora green and clean glacial tones |
| 8 | **Catppuccin Mocha** | Warm soothing pastel palette with mauve, peach and teal |
| 9 | **Monokai Pro** | High-energy magenta, acid yellow, and bright mint |
| 10 | **Gruvbox Dark** | Warm retro groove with gold, aqua, and autumn orange |
| 11 | **Kali Linux Dark** | Offensive security dark terminal with stealth crimson & ice blue |
| 12 | **Apple Classic** | Minimalist Cupertino styling with silver, blue and emerald |
| 13 | **Sunset Horizon** | Warm twilight gradient with flamingo pink and golden mango |
| 14 | **Solarized Dark** | Precision engineered scientific palette with teal and cyan |
| 15 | **Synthwave 84** | Outrun aesthetic with magenta laser and radioactive cyan |

### Custom JSON Themes
Place custom `.json` themes in `~/.config/macpulse/themes/mytheme.json` or `./themes/`:

```json
{
  "name": "My Custom Theme",
  "desc": "Custom color palette for MacPulse",
  "border": "#ff007f",
  "header_bg": "#1f2335",
  "header_fg": "#00f0ff",
  "title": "#ffe600",
  "accent": "#00f0ff",
  "accent2": "#ff007f",
  "cpu_color": "#00f0ff",
  "gpu_color": "#ff007f",
  "mem_color": "#ffe600",
  "net_down": "#00ff66",
  "net_up": "#ff7700",
  "text": "#ffffff",
  "dim": "#707090",
  "warn": "#ffe600",
  "crit": "#ff003c",
  "proc_sel_bg": "#3d005c",
  "proc_sel_fg": "#00ffff"
}
```

---

## ⌨️ Interactive Keybindings & Controls

| Key | Action | Description |
| :--- | :--- | :--- |
| **`q`** / **`Ctrl+C`** | **Quit** | Cleanly restores terminal and exits |
| **`t`** | **Theme Picker** | Opens interactive Theme Customizer popup with live selection |
| **`T`** | **Cycle Themes** | Instantly cycles to next color theme |
| **`v`** | **Cycle Layouts** | Cycles 10 full-screen views: **Full** → **Compact** → **Zen** → **Overview** → **CPU** → **GPU** → **Mem** → **Net** → **Bento** → **Rail** |
| **`g`** | **Graph Mode** | Cycles history graphs: **Split** (CPU/GPU) → **Stacked** → **Dual-Series** → **Net Throughput** |
| **`Tab`** | **Toggle Procs** | Toggles the Process Table on/off to maximize graphical history plots |
| **`c`** | **Sort %CPU** | Sorts process table by %CPU (descending) |
| **`m`** | **Sort %MEM** | Sorts process table by Memory usage (descending) |
| **`p`** | **Sort PID** | Sorts process table by Process ID |
| **`n`** | **Sort Name** | Sorts process table alphabetically by command name |
| **`r`** | **Reverse Sort** | Inverts the current sort order (ascending/descending) |
| **`/`** | **Search Filter** | Live filter processes by process name or username |
| **`x`** / **`K`** | **Kill Process** | Opens confirmation dialog to send `SIGTERM` to selected process |
| **`↑` / `↓` / `j` / `k`** | **Scroll** | Scroll up and down through the process list |
| **`PgUp` / `PgDn`** | **Page Scroll** | Scroll process list by 10 rows |
| **`+` / `-`** | **Refresh Speed** | Adjusts refresh interval (0.25s up to 5.0s) |
| **`h`** / **`?`** | **Help Modal** | Opens the interactive shortcut reference dialog |
| **`Esc`** | **Cancel / Close** | Closes active modal or cancels active search filter |

---

## 🚀 Installation & Build

### Option 1: Quick Installer
```bash
./install.sh
```

### Option 2: Build with `make`
```bash
make
sudo make install
```

### Option 3: Direct Clang Compilation
```bash
clang -O3 \
    -framework CoreFoundation \
    -framework Foundation \
    -framework IOKit \
    -framework Metal \
    -Isrc \
    src/theme.c src/telemetry.m src/ui.c src/main.m \
    -o bin/macpulse
```

---

## 💻 CLI Flags

```
Usage: macpulse [OPTIONS]

Options:
  -t, --theme <name>     Select color theme on launch (e.g. matrix, cyberpunk, dracula, nord)
  -r, --rate <seconds>   Set refresh rate in seconds (default: 1.0, min: 0.2)
  -V, --view, --layout   Set dashboard layout (full, compact, zen, overview, cpu, gpu, mem, net, bento, rail)
  -l, --list-themes      List all available built-in and custom themes (15 themes)
  -h, --help             Show help message
  -v, --version          Show version information
```

---

## 📜 License

MIT License — Copyright (c) 2026 Atanu Roy.  
Designed and built natively for macOS and Apple Silicon.
