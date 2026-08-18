#!/usr/bin/env bash
# flash.sh — compile and flash ghost_device to an ESP32-S3
#
# Usage:
#   bash flash.sh                  # auto-detect port
#   bash flash.sh /dev/cu.usbmodem2101
#   bash flash.sh COM5             # Windows / WSL

set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "$0")" && pwd)"
FQBN="esp32:esp32:esp32s3:FlashSize=4M,PartitionScheme=default"
ESP32_CORE_VERSION="3.x"

# ── Colours ─────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[*]${NC} $*"; }
warn()  { echo -e "${YELLOW}[!]${NC} $*"; }
fatal() { echo -e "${RED}[x]${NC} $*"; exit 1; }

# ── 1. arduino-cli ──────────────────────────────────────────────────────────
if ! command -v arduino-cli &>/dev/null; then
    info "arduino-cli not found — installing..."
    if [[ "$OSTYPE" == "darwin"* ]] && command -v brew &>/dev/null; then
        brew install arduino-cli
    else
        curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
        export PATH="$HOME/bin:$PATH"
    fi
fi
info "arduino-cli: $(arduino-cli version)"

# ── 2. ESP32 Arduino core ────────────────────────────────────────────────────
BOARD_URL="https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
arduino-cli config set board_manager.additional_urls "$BOARD_URL" 2>/dev/null || true
arduino-cli core update-index --config-file <(echo "board_manager:
  additional_urls:
    - $BOARD_URL") 2>/dev/null || arduino-cli core update-index

if ! arduino-cli core list | grep -q "esp32:esp32"; then
    info "Installing ESP32 Arduino core (this downloads ~200 MB, once only)..."
    arduino-cli core install esp32:esp32
else
    info "ESP32 core already installed: $(arduino-cli core list | grep esp32:esp32)"
fi

# ── 3. Port detection ────────────────────────────────────────────────────────
PORT="${1:-}"
if [[ -z "$PORT" ]]; then
    info "Scanning for ESP32 USB port..."
    # Try common patterns in order of likelihood
    for pattern in \
        "/dev/cu.usbmodem*" \
        "/dev/cu.usbserial-*" \
        "/dev/ttyUSB*" \
        "/dev/ttyACM*"; do
        matches=( $pattern ) 2>/dev/null || true
        if [[ ${#matches[@]} -gt 0 && -e "${matches[0]}" ]]; then
            PORT="${matches[0]}"
            break
        fi
    done
    # Also try arduino-cli board list
    if [[ -z "$PORT" ]]; then
        PORT=$(arduino-cli board list 2>/dev/null | grep -i "esp32\|Unknown" \
               | awk '{print $1}' | head -1)
    fi
fi

[[ -z "$PORT" ]] && fatal "No ESP32 port found. Connect the board and retry, or pass the port as an argument:\n  bash flash.sh /dev/cu.usbmodem2101"
info "Using port: $PORT"

# ── 4. Compile ───────────────────────────────────────────────────────────────
info "Compiling ghost_device (this takes 1-2 minutes on first build)..."
arduino-cli compile --fqbn "$FQBN" --warnings none "$SKETCH_DIR"

# ── 5. Flash ─────────────────────────────────────────────────────────────────
info "Flashing to $PORT..."
arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$SKETCH_DIR"

echo ""
echo -e "${GREEN}Done.${NC} ghost_device is running."
echo ""
echo "LED behaviour (GPIO48 WS2812B):"
echo "  Brief white flash   = all init succeeded"
echo "  Cyan breathing      = running normally"
echo "  Blue flickers       = Wi-Fi probe bursts firing"
echo "  Purple every 2.5 s  = BLE identity rotating"
echo "  Solid red           = Wi-Fi init failed (check board FQBN)"
echo ""
echo "Verify Wi-Fi probes (Linux, monitor-mode adapter required):"
echo "  sudo iw wlan0 set type monitor && sudo ip link set wlan0 up"
echo "  sudo tcpdump -i wlan0 'wlan type mgt subtype probe-req' -e"
echo "  Look for locally-administered MACs (02:xx or fe:xx) in 20 ms bursts."
echo ""
echo "Verify BLE:"
echo "  pip install bleak && python3 -c \\"
echo "    \"import asyncio,bleak; asyncio.run(bleak.BleakScanner.discover(5))\""
echo "  Look for iPhone 16 Pro, Galaxy S25, MacBook Air, etc."
