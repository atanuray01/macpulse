# MacPulse ◈

> **Ultra-Fast, Real-Time Apple Silicon System Monitor & Dashboard for macOS Terminal**  
> *Sudo-free native telemetry, rolling graphs, unified memory breakdown, interactive activity monitor & 12 pro color themes.*

---

```
  __  __            _____      _           
 |  \/  |          |  __ \    | |          
 | \  / | __ _  ___| |__) |  _| |___  ___  
 | |\/| |/ _` |/ __|  ___/ | | | / __|/ _ \ 
 | |  | | (_| | (__| |   | |_| | \__ \  __/ 
 |_|  |_|\__,_|\___|_|    \__,_|_|___/\___| 
                                           
        ◈ Apple Silicon Terminal Dashboard ◈
```

---

## ⚡ Highlights & Comparison vs Mactop

| Feature | `mactop` | `btop` / `htop` | **`MacPulse` (This App)** |
| :--- | :---: | :---: | :---: |
| **No Sudo Required** | ✅ Yes | ❌ Needs Root for GPU | **✅ Yes (100% Native APIs)** |
| **Apple Silicon M1 - M5 Support** | ✅ Yes | ⚠️ Partial | **✅ Yes (P-Cores, E-Cores, SoC)** |
| **CPU Usage History Graph** | ❌ None | ⚠️ Basic | **✅ Multi-Level Unicode Sparklines** |
| **GPU Device, Renderer & Tiler %** | ⚠️ Basic | ❌ Missing on Mac | **✅ Full `IOAccelerator` Telemetry** |
| **GPU VRAM In-Use vs Allocated** | ❌ None | ❌ None | **✅ Live VRAM Breakdown** |
| **Real-time Thermal Die Sensors** | ⚠️ Avg only | ❌ None | **✅ CPU, GPU, SoC Max, SSD & Battery** |
| **System Thermal Throttling State**| ❌ None | ❌ None | **✅ Nominal / Fair / Serious / Critical** |
| **Unified Memory Breakdown** | ⚠️ Basic | ⚠️ Standard RAM | **✅ App, Wired, Compressed, Cache, Swap** |
| **Network Speed & Dual Sparklines**| ❌ None | ⚠️ Basic | **✅ Live ▼ Down / ▲ Up + Rolling Graph** |
| **Activity Monitor / Process Table**| ❌ None | ✅ Yes | **✅ Live %CPU, %MEM, Search & Sort** |
| **Theme Customization** | ❌ Minimal | ⚠️ Fixed Palettes | **✅ 12 Pro Themes + Live Modal + JSON** |
| **Zero-Flicker Double Buffering** | ⚠️ Curses | ⚠️ Redraws | **✅ Atomic Double-Buffered Frames** |
| **Binary Footprint & CPU Usage** | ~15MB | ~25MB | **< 1MB binary, < 0.3% CPU usage** |

---

## 📸 Dashboard Layout Overview

MacPulse organizes your Mac's telemetry into a unified, high-density dashboard featuring **CPU, GPU, Unified Memory, and Network Speed** readings alongside **authentic ttyplot-style graphical history plots**:

```
◈ MacPulse • Apple M4 • 10 Cores (6E+4P) • 8 GPU • v1.0.0            ⚡ 89% • Nominal • Up: 4d 2h • 16:50:00
┌─ CPU MONITOR • 34.0°C ─────┐┌─ GPU & VRAM ───────────────┐┌─ UNIFIED MEMORY ───────────┐┌─ NETWORK SPEED ────────────┐
│ [||||...............]  18% ││ [||.................]  12% ││ [|||||||||.........]  53%  ││ Download: ▼  3.42 MB/s     │
│ User: 12%  Sys:  6%        ││ Renderer: 12%  Tiler:  4%  ││ App: 5.2GB  Wired: 1.8GB   ││ Upload:   ▲ 480.2 KB/s     │
│ Load: 1.64 1.52 1.48       ││ VRAM: 335.3MB / 1.3GB      ││ Comp: 1.6GB  Cache: 5.0GB  ││ In: 14.2GB  Out: 2.4GB     │
│ 10 Cores (6E + 4P)         ││ 8 GPU Cores • Metal 3      ││ Free: 1.9GB  Swap: 0B      ││ Disk IO: R 16KB/s W 4KB/s   │
└────────────────────────────┘└────────────────────────────┘└────────────────────────────┘└────────────────────────────┘
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
 [q]Exit [t]Themes [g]Graphs:Split [Tab]HideProcs [c]CPU [m]Mem [p]PID [/]Search [x]Kill [+/-]1.0s
```

---

## 🚀 Installation & Quick Start

### Option 1: Quick One-Line Installer
Run the automated installer script:
```bash
./install.sh
```

### Option 2: Build with `make`
Compile and install using Apple Clang:
```bash
make
sudo make install
```

### Option 3: Direct Compilation
Compile with a single command (requires zero external libraries, links standard macOS frameworks):
```bash
clang -O3 \
    -framework CoreFoundation \
    -framework Foundation \
    -framework IOKit \
    -Isrc \
    src/theme.c src/telemetry.m src/ui.c src/main.m \
    -o bin/macpulse
```

Run MacPulse immediately:
```bash
./bin/macpulse
```

---

## ⌨️ Interactive Keybindings & Controls

| Key | Action | Description |
| :--- | :--- | :--- |
| **`q`** / **`Ctrl+C`** | **Quit** | Cleanly restores terminal and exits |
| **`t`** | **Theme Picker** | Opens interactive Theme Customizer popup with live swatches |
| **`T`** | **Cycle Themes** | Instantly cycles to the next color theme |
| **`g`** | **Graph Mode** | Cycles ttyplot graph modes: **Split** (CPU/GPU) → **Stacked** → **Dual-Series** → **Net Throughput** |
| **`Tab`** | **Toggle Procs** | Toggles the Process Table on/off to maximize the graphical history plots |
| **`v`** | **View Mode** | Cycles view layout modes (**Full Dashboard** → **Compact** → **Zen Graphs**) |
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

## 🎨 Theme Customization (12 Built-in Themes + Custom JSON)

MacPulse includes 12 handcrafted 24-bit Truecolor RGB themes:

1. **Cyberpunk 2077** — High contrast neon pink, electric yellow and laser cyan
2. **Tokyo Night** — Modern clean indigo, soft cyan, purple and warm orange
3. **Dracula Pro** — Iconic gothic dark theme with vibrant pastels
4. **Nord Arctic** — Arctic blue, aurora green and clean glacial tones
5. **Catppuccin Mocha** — Warm soothing pastel palette with mauve, peach and teal
6. **Monokai Pro** — High-energy magenta, acid yellow, and bright mint
7. **Gruvbox Dark** — Warm retro groove with gold, aqua, and autumn orange
8. **Matrix Hacker** — Digital rain monochrome phosphor greens
9. **Solarized Dark** — Precision engineered scientific palette with teal and cyan
10. **Apple Classic** — Minimalist Cupertino styling with silver, blue and emerald
11. **Sunset Horizon** — Warm twilight gradient with flamingo pink and golden mango
12. **Synthwave 84** — Outrun aesthetic with magenta laser and radioactive cyan

### Creating Custom Themes

Place your custom `.json` theme file in `~/.config/macpulse/themes/mytheme.json` or `./themes/`:

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

Launch with your custom theme:
```bash
macpulse --theme "My Custom Theme"
```

List all available themes:
```bash
macpulse --list-themes
```

---

## 🛠️ Native Architecture (Zero Sudo Required)

MacPulse uses zero command-line subprocess wrappers and requires **no sudo privileges** by interacting directly with low-level Apple Darwin C/Mach kernel APIs:

- **Mach Kernel Host API (`host_processor_info`)**: Gathers per-core ticks (user, system, idle, nice) across all Performance and Efficiency cores.
- **IOKit `IOAccelerator` (`PerformanceStatistics`)**: Reads real-time GPU Device %, Renderer %, Tiler % and allocated VRAM bytes directly from the Apple Silicon GPU driver.
- **`IOHIDEventSystemClient`**: Direct access to Apple SoC thermal die sensors (`PMU tdie`, `PMU2 tdie`) for CPU, GPU, SSD, and battery temperatures.
- **`NSProcessInfo.thermalState`**: Reads macOS system thermal throttling state (`Nominal`, `Fair`, `Serious`, `Critical`).
- **Mach VM Statistics (`host_statistics64`) & `sysctl`**: Extracts exact App, Wired, Compressed, Cached, and Swap memory breakdown.
- **POSIX `getifaddrs`**: Interrogates network interfaces for real-time input/output byte deltas.
- **Darwin `libproc.h` (`proc_listpids`, `proc_pidinfo`)**: High-speed, non-blocking process enumeration with CPU delta and resident memory sizing.
- **Double-Buffered Atomic Rendering**: Assembles complete ANSI frames in a dynamic memory buffer and writes them out in a single `write(2)` syscall to guarantee zero screen tearing.

---

## 💻 CLI Flags

```
Usage: macpulse [OPTIONS]

Options:
  -t, --theme <name>     Select color theme on launch (e.g. cyberpunk, dracula, nord)
  -r, --rate <seconds>   Set refresh rate in seconds (default: 1.0, min: 0.2)
  -l, --list-themes      List all available built-in and custom themes
  -h, --help             Show help message
  -v, --version          Show version information
```

---

## 📜 License

MIT License — Copyright (c) 2026 Atanu Roy.  
Designed and built for Apple Silicon Mac users.
