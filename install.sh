#!/usr/bin/env bash
set -e

echo "=================================================="
echo "          Installing MacPulse for macOS           "
echo "  Apple Silicon Native Terminal System Monitor    "
echo "=================================================="

# Check OS
if [[ "$(uname)" != "Darwin" ]]; then
    echo "Error: MacPulse is designed specifically for macOS (Apple Silicon)."
    exit 1
fi

ARCH="$(uname -m)"
if [[ "$ARCH" != "arm64" ]]; then
    echo "Note: Running on $ARCH. MacPulse is optimized for Apple Silicon (M1/M2/M3/M4/M5)."
fi

# Detect install directory
if [[ -w "/usr/local/bin" ]]; then
    INSTALL_DIR="/usr/local/bin"
elif [[ -d "$HOME/.local/bin" ]] || mkdir -p "$HOME/.local/bin"; then
    INSTALL_DIR="$HOME/.local/bin"
else
    INSTALL_DIR="/usr/local/bin"
fi

echo "==> Building MacPulse binary..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

clang -O3 \
    -framework CoreFoundation \
    -framework Foundation \
    -framework IOKit \
    -Isrc \
    src/theme.c src/telemetry.m src/ui.c src/main.m \
    -o bin/macpulse

echo "==> Installing binary to $INSTALL_DIR/macpulse..."
if [[ -w "$INSTALL_DIR" ]]; then
    cp bin/macpulse "$INSTALL_DIR/macpulse"
    chmod +x "$INSTALL_DIR/macpulse"
else
    echo "Requires sudo to install into $INSTALL_DIR:"
    sudo cp bin/macpulse "$INSTALL_DIR/macpulse"
    sudo chmod +x "$INSTALL_DIR/macpulse"
fi

# Copy themes to ~/.config/macpulse/themes
THEME_DIR="$HOME/.config/macpulse/themes"
mkdir -p "$THEME_DIR"
if [[ -d "$SCRIPT_DIR/themes" ]]; then
    cp -r "$SCRIPT_DIR/themes/"* "$THEME_DIR/"
    echo "==> Installed themes to $THEME_DIR"
fi

echo ""
echo "=================================================="
echo "  ✓ MacPulse successfully installed!"
echo "  Run it anytime by typing:"
echo ""
echo "      macpulse"
echo ""
echo "  Or check themes with:"
echo ""
echo "      macpulse --list-themes"
echo "=================================================="
