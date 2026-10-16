CC ?= clang
CFLAGS ?= -O3 -Wall -Wextra -Wno-unused-parameter
FRAMEWORKS = -framework CoreFoundation -framework Foundation -framework IOKit
INCLUDES = -Isrc

SRC = src/theme.c src/telemetry.m src/ui.c src/main.m
TARGET = bin/macpulse
PREFIX ?= /usr/local
INSTALL_BIN = $(PREFIX)/bin

.PHONY: all build clean install uninstall release

all: build

build: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(FRAMEWORKS) $(INCLUDES) $(SRC) -o $(TARGET)
	@echo "✓ Successfully built MacPulse at $(TARGET)"

clean:
	rm -rf bin build dist *.tar.gz
	@echo "✓ Cleaned build artifacts"

install: build
	@mkdir -p $(INSTALL_BIN)
	install -m 755 $(TARGET) $(INSTALL_BIN)/macpulse
	@echo "✓ Installed macpulse to $(INSTALL_BIN)/macpulse"

uninstall:
	rm -f $(INSTALL_BIN)/macpulse
	@echo "✓ Uninstalled macpulse from $(INSTALL_BIN)/macpulse"

release: clean build
	@mkdir -p dist
	@cp $(TARGET) dist/
	@cp -r themes dist/
	@cp README.md LICENSE config.example.json install.sh dist/
	tar -czf macpulse-v1.0.0-darwin-arm64.tar.gz -C dist .
	@echo "✓ Created release archive: macpulse-v1.0.0-darwin-arm64.tar.gz"
