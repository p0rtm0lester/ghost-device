# ghost_device

ESP32 firmware that simulates 100 independent phones and laptops over Wi-Fi and Bluetooth LE simultaneously.

Each virtual device has its own random MAC address and "knows" a set of nearby SSIDs — exactly like a real device that has connected to those networks before. From the perspective of passive scanners, Kismet captures, or proximity detection systems, the traffic looks like a crowd of devices.

## What it does

**Wi-Fi (802.11 probe requests)**
- 100 virtual devices, each with a unique locally-administered MAC (`02:xx:xx:xx:xx:xx`)
- Each device probes for 3–8 SSIDs drawn from a 1,500-entry default list
- Sends realistic 802.11n frames: SSID IE, Supported Rates, HT Capabilities, Extended Capabilities
- Cycles through all 11 2.4 GHz channels
- ~1/3 of devices also emit wildcard probes (empty SSID), as real phones do

**Bluetooth LE**
- Rotates through 100 BLE identities, changing every 2.5 seconds
- Each identity gets a fresh random static address (spec-compliant top-2-bits)
- Simulates iPhones, Android phones, MacBooks, Windows laptops, and Pixels
- Sets Apple / Samsung / Microsoft manufacturer-specific data bytes
- Non-connectable advertisements at 100–150 ms intervals

**MAC randomization (iOS 14+ / Android 10+ behavior)**

Modern phones use two independent MAC pools for probe requests — directed probes and wildcard probes use different addresses, and both rotate on different schedules. This firmware replicates that behavior exactly:

| Probe type | MAC | Rotation |
|---|---|---|
| Directed (specific SSID) | Per-device MAC | Every 10 minutes |
| Wildcard (empty SSID) | Shared, separate MAC | Every 30 seconds |
| BLE advertisements | Per-identity random address | Every 2.5 seconds |

Using a different MAC for wildcard vs. directed probes is the key behavior that defeats cross-session fingerprinting — a passive observer cannot correlate the two traffic types by MAC alone.

## Hardware

Tested on **ESP32-S3FH4R2** (4 MB flash, 2 MB PSRAM). Should work on any ESP32 variant with sufficient flash:

| Board | Wi-Fi | BLE | RGB LED | Notes |
|---|---|---|---|---|
| ESP32-S3 Dev Module | ✅ | ✅ | ✅ GPIO48 | Primary test target |
| ESP32 (original) | ✅ | ✅ | ❌ | No built-in RGB |
| ESP32-S2 | ✅ | ❌ | ❌ | Wi-Fi only; comment out BLE init |
| ESP32-C3 | ✅ | ✅ | ❌ | Untested |

Minimum flash: 4 MB. No PSRAM required.

## RGB LED status

On boards with a built-in WS2812B RGB LED (GPIO48 on ESP32-S3 dev modules), the LED provides live activity feedback. No external library is needed — the firmware uses `rgbLedWrite()` from the ESP32 Arduino Core 3.x.

| Color | Event |
|---|---|
| 🔵 Cyan, slow breathing | Idle — running normally |
| 🔵 Blue | Wi-Fi probe burst firing |
| 🟣 Purple | BLE identity rotated (every 2.5 s) |
| 🟡 Yellow | Wildcard MAC rotated (every 30 s) |
| 🟢 Green | All directed MACs rotated (every 10 min) |

During normal operation the LED flickers blue constantly (probing is nearly continuous), with purple flashes every 2.5 seconds as BLE identities rotate. Yellow appears every 30 seconds and green appears every 10 minutes.

## Setup

### 1. Install the ESP32 Arduino core

In Arduino IDE: **File → Preferences → Additional boards URLs**, add:

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Then **Tools → Board → Boards Manager** → install **esp32 by Espressif Systems** (3.x or later).

Or with arduino-cli:

```bash
arduino-cli core install esp32:esp32
```

### 2. (Optional) Generate a custom SSID list

The firmware ships with 1,500 built-in SSIDs. To replace them with real SSIDs from your area:

```bash
cd tools/

# Pull from WiGLE API (get credentials at wigle.net/account)
python3 wigle_fetch.py \
    --wigle \
    --lat 40.014 --lon -105.270 \
    --api-name YOUR_NAME --api-token YOUR_TOKEN \
    --output ../ssid_list.h

# Parse a WiGLE CSV export
python3 wigle_fetch.py --csv ~/Downloads/WigleWifi.csv --output ../ssid_list.h

# Generate 2000 synthetic SSIDs (no account needed)
python3 wigle_fetch.py --gen 2000 --output ../ssid_list.h

# Combine all sources
python3 wigle_fetch.py \
    --wigle --lat 40.014 --lon -105.270 \
    --api-name NAME --api-token TOKEN \
    --csv ~/Downloads/WigleWifi.csv \
    --gen 500 \
    --max 3000 \
    --output ../ssid_list.h
```

The SSID list is stored in flash (not RAM), so it can be as large as your flash partition allows.

### 3. Flash

**Arduino IDE**
1. Open `ghost_device.ino`
2. **Tools → Board** → select your ESP32 variant
3. **Tools → Port** → select your device
4. Upload

**arduino-cli**
```bash
# ESP32-S3
arduino-cli compile --fqbn esp32:esp32:esp32s3 ghost_device/
arduino-cli upload  --fqbn esp32:esp32:esp32s3 --port /dev/cu.usbmodem* ghost_device/

# ESP32 original
arduino-cli compile --fqbn esp32:esp32:esp32dev ghost_device/
arduino-cli upload  --fqbn esp32:esp32:esp32dev --port /dev/cu.usbserial-* ghost_device/
```

### 4. Verify

Open Serial Monitor at **115200 baud**. Expected output:

```
ghost_device starting — 100 virtual devices, 1500 SSIDs
Free heap: 284672 bytes
Initialized 100 virtual devices, 1500 SSIDs loaded
Wi-Fi injection ready
BLE ghost ready
Running. Free heap after init: 198432 bytes
```

On boards with an RGB LED, the cyan breathing heartbeat confirms the firmware is running before any wireless traffic appears.

In a Wi-Fi scanner (Kismet, airodump-ng) you should immediately see probe requests from locally-administered MACs. In a BLE scanner (nRF Connect, Wireshark + BT adapter) you should see advertisements cycling through device names like "iPhone 16 Pro", "Galaxy S25", "MacBook Air", etc.

## Configuration

All tuning constants are at the top of `ghost_device.ino`:

| Constant | Default | Effect |
|---|---|---|
| `NUM_VIRTUAL_DEVICES` | `100` | Number of simultaneous device identities |
| `PROBE_BURST_DELAY_MS` | `20` | ms between probes in a burst |
| `PROBE_DEVICE_DELAY_MS` | `80` | ms before cycling to the next device |
| `BLE_ROTATE_MS` | `2500` | ms per BLE identity |
| `DIRECTED_MAC_ROTATE_MS` | `600000` | ms between directed MAC rotations (10 min) |
| `WILDCARD_MAC_ROTATE_MS` | `30000` | ms between wildcard MAC rotations (30 s) |

**More aggressive** (denser traffic):

```cpp
#define PROBE_DEVICE_DELAY_MS   40
#define BLE_ROTATE_MS           1000
```

**Lower profile** (thinner traffic, less CPU):

```cpp
#define NUM_VIRTUAL_DEVICES     50
#define PROBE_DEVICE_DELAY_MS   200
#define BLE_ROTATE_MS           5000
```

## Device mix

Edit `device_profiles.h` to change the simulated device distribution:

```cpp
const uint8_t DTYPE_WEIGHTS[] = {
    35,   // iPhone   (35%)
    30,   // Android  (30%)
    10,   // MacBook  (10%)
    15,   // Windows  (15%)
    10,   // Pixel    (10%)
};
```

Add device names to `DEVICE_NAMES[]` or new manufacturer-specific data payloads to `MFR_DATA_BY_TYPE[]`.

## Project structure

```
ghost_device/
├── ghost_device.ino      Main sketch — device registry, MAC rotation, LED, task scheduling
├── wifi_probe.h/.cpp     802.11 probe request frame builder and injector
├── ble_ghost.h/.cpp      BLE advertiser with rotating random static addresses
├── device_profiles.h     Device names, BLE manufacturer data, type weights
├── ssid_list.h           SSID list stored in flash (1,500 entries default)
└── tools/
    └── wigle_fetch.py    Build ssid_list.h from WiGLE API, CSV, or synthetic generation
```

## Legal notice

This tool is intended for **authorized security research, network testing, privacy research, and educational use only**.

Operating this firmware may violate local regulations if used:
- On networks or in environments where you do not have explicit authorization
- To interfere with legitimate wireless communications
- To evade detection in an unauthorized context

The authors are not responsible for misuse. You are solely responsible for ensuring your use complies with applicable laws.

## License

MIT — see [LICENSE](LICENSE).
