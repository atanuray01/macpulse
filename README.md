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

MacPulse organizes your entire Mac's telemetry into a single, unified, high-density terminal dashboard:

```
◈ MacPulse v1.0.0 │ Apple M4 (10 Cores: 4P + 6E) │ Mac16,12   ⚡ 89% │ Nominal │ Up: 4d 2h │ 13:30:00
╭─ CPU MONITOR 34.8°C (Peak: 35.6°C) ─────────────────╮╭─ GPU & THERMAL 33.6°C (SoC: 36.4°C) ─────────────────╮
│ Total: ████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   12.4% ││ Device: ██████░░░░░░░░░░░░░░░░░░░░░░░░░░░░░   20.0% │
│ User:  8.0% Sys:  4.4% Idle: 87.6% Load: 1.82 1.65 ││ Renderer: 20.0% Tiler: 20.0% VRAM: 512 MB / 1.69 GB │
│ P-Cores: 0:18% 1:12% 2: 8% 3:15%                    ││ Thermal: [Nominal] SSD: 34.0°C Battery: 32.0°C        │
│ E-Cores: 0: 4% 1: 2% 2: 1% 3: 5% 4: 2% 5: 3%        ││ GPU Load Rolling History Graph:                       │
│    ▂▃▅▄▃▂ ▂▃▅▆▇█▇▆▅▄▃▂  ▂▃▄▅▆▇▆▅▄▃▂                 ││    ▂▃▄▅▄▃▂  ▂▃▄▅▆▇▆▅▄▃▂  ▂▃▄▅▆▇▆▅▄▃▂                 │
╰─────────────────────────────────────────────────────╯╰─────────────────────────────────────────────────────╯
╭─ UNIFIED MEMORY 8.41 GB / 16.00 GB (52.6%) ─────────╮╭─ NETWORK SPEED ▼  1.24 MB/s  ▲ 240.5 KB/s ───────────╮
│ RAM:  █████████████████░░░░░░░░░░░░░░░░░  52.6%      ││ Download: ▼  1.24 MB/s  Upload: ▲ 240.5 KB/s          │
│ App Memory:   5.30 GB   Wired Memory: 1.65 GB       ││ Total In:  3.68 GB      Total Out: 481.31 MB        │
│ Cached Files: 5.27 GB   Compressed:   1.47 GB       ││ Bandwidth Rolling Graph:                              │
│ Breakdown: [App: 5.30 GB] [Wired: 1.65 GB] [Comp...] ││    ▂▃▅▆▇█▇▆▅▄▃▂  ▂▃▄▅▆▇▆▅▄▃▂  ▂▃▄▅▆▇▆▅▄▃▂             │
╰─────────────────────────────────────────────────────╯╰─────────────────────────────────────────────────────╯
╭─ ACTIVITY MONITOR 477 processes ─────────────────────────────────────────────────────────────────────────────╮
│ PID     USER           %CPU     %MEM     RESIDENT     STATE    COMMAND                                       │
│ 11405   atanuray       14.2%     1.9%    305.23 MB    RUN      python3.14                                    │
│ 11709   atanuray        8.5%    10.2%      1.63 GB    RUN      com.apple.WebKit.WebContent                   │
│ 639     atanuray        1.2%     0.6%     94.41 MB    RUN      ControlCenter                                 │
│ 645     atanuray        0.8%     0.7%    120.23 MB    RUN      Finder                                        │
╰─────────────────────────────────────────────────────────────────────────────────────────────────────────────╯
 [q]Quit [t]Theme [c]CPU [m]Mem [p]PID [n]Name [/]Filter [x]Kill [h]Help [+/-]1.0s
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
