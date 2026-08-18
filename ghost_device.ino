/*
 * ghost_device — ESP32 Wireless Device Simulator
 *
 * Simulates 100 independent phones and laptops by broadcasting:
 *   - 802.11 probe request frames (Wi-Fi management frames)
 *   - BLE advertisements with realistic manufacturer data
 *
 * Each virtual device has its own locally-administered MAC address and
 * "knows" a subset of SSIDs from the loaded list, just like a real
 * device that has connected to those networks before.
 *
 * Hardware: ESP32, ESP32-S3 (or any ESP32 variant with BLE)
 * Framework: Arduino (ESP32 Arduino Core 3.x)
 *
 * See README.md for setup, configuration, and legal notice.
 */

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "wifi_probe.h"
#include "ble_ghost.h"
#include "device_profiles.h"
#include "ssid_list.h"

static const char* TAG = "ghost";

// ── Configuration ──────────────────────────────────────────────────────────────

#define NUM_VIRTUAL_DEVICES     100   // simultaneous device identities

// Wi-Fi probe timing
#define PROBE_BURST_COUNT       3     // directed probes sent per SSID per cycle
#define PROBE_BURST_DELAY_MS    20    // ms between probes in a burst
#define PROBE_DEVICE_DELAY_MS   80    // ms before moving to the next device

// BLE rotation: how long each virtual device "exists" before swapping identity
#define BLE_ROTATE_MS           2500  // ms per BLE identity (2.5 seconds)

// 802.11 channels to cycle through (1–11 = 2.4 GHz)
static const uint8_t CHANNELS[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
#define NUM_CHANNELS (sizeof(CHANNELS) / sizeof(CHANNELS[0]))

// ── Virtual Device Registry ────────────────────────────────────────────────────

struct VirtualDevice {
    uint8_t mac[6];        // Wi-Fi source MAC (locally administered, random per boot)
    uint8_t ssid_start;    // first index into ssid_list[] for this device
    uint8_t ssid_count;    // how many SSIDs this device "knows" (3–8)
    uint8_t channel;       // preferred probe channel
};

static VirtualDevice devices[NUM_VIRTUAL_DEVICES];

// Generates a random locally-administered unicast MAC.
// Bit 0 of byte 0 = 0 (unicast), bit 1 of byte 0 = 1 (locally administered).
static void gen_mac(int index, uint8_t* mac) {
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random() ^ ((uint32_t)index * 2654435761UL);
    mac[0] = (r1 & 0xfe) | 0x02;
    mac[1] = (r1 >>  8) & 0xff;
    mac[2] = (r1 >> 16) & 0xff;
    mac[3] = r2         & 0xff;
    mac[4] = (r2 >>  8) & 0xff;
    mac[5] = (r2 >> 16) & 0xff;
}

static void init_virtual_devices() {
    for (int i = 0; i < NUM_VIRTUAL_DEVICES; i++) {
        gen_mac(i, devices[i].mac);

        devices[i].ssid_count = 3 + (esp_random() % 6);
        devices[i].ssid_start = (i * (NUM_SSIDS / NUM_VIRTUAL_DEVICES)) % NUM_SSIDS;
        if (devices[i].ssid_start + devices[i].ssid_count > NUM_SSIDS)
            devices[i].ssid_start = NUM_SSIDS - devices[i].ssid_count;

        devices[i].channel = CHANNELS[i % NUM_CHANNELS];
    }
    ESP_LOGI(TAG, "Initialized %d virtual devices, %d SSIDs loaded",
             NUM_VIRTUAL_DEVICES, NUM_SSIDS);
}

// ── Wi-Fi Probe Task ───────────────────────────────────────────────────────────

static void wifi_probe_task(void* arg) {
    int dev_idx     = 0;
    int channel_idx = 0;

    while (true) {
        VirtualDevice& dev = devices[dev_idx];

        // Send directed probes for each SSID this device "remembers"
        for (int s = 0; s < dev.ssid_count; s++) {
            int ssid_idx = (dev.ssid_start + s) % NUM_SSIDS;
            const char* ssid = ssid_list[ssid_idx];
            wifi_send_probe_request(dev.mac, ssid, (uint8_t)strlen(ssid), dev.channel);
            vTaskDelay(pdMS_TO_TICKS(PROBE_BURST_DELAY_MS));
        }

        // ~1/3 of devices also send a wildcard probe (empty SSID), like most real phones
        if ((dev_idx % 3) == 0) {
            uint8_t hop = CHANNELS[channel_idx % NUM_CHANNELS];
            wifi_send_probe_request(dev.mac, nullptr, 0, hop);
            channel_idx++;
        }

        vTaskDelay(pdMS_TO_TICKS(PROBE_DEVICE_DELAY_MS));
        dev_idx = (dev_idx + 1) % NUM_VIRTUAL_DEVICES;
    }
}

// ── BLE Ghost Task ─────────────────────────────────────────────────────────────

static void ble_task(void* arg) {
    int dev_idx = 0;

    while (true) {
        ble_ghost_advertise(dev_idx);
        vTaskDelay(pdMS_TO_TICKS(BLE_ROTATE_MS));
        dev_idx = (dev_idx + 1) % NUM_VIRTUAL_DEVICES;
    }
}

// ── Setup ──────────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(500);

    ESP_LOGI(TAG, "ghost_device starting — %d virtual devices, %d SSIDs",
             NUM_VIRTUAL_DEVICES, NUM_SSIDS);
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_event_loop_create_default();
    init_virtual_devices();

    if (!wifi_inject_init()) {
        ESP_LOGE(TAG, "Wi-Fi init failed, halting");
        while (true) delay(1000);
    }

    if (!ble_ghost_init())
        ESP_LOGW(TAG, "BLE init failed — running Wi-Fi only");

    // Wi-Fi task on core 0, BLE task on core 1
    xTaskCreatePinnedToCore(wifi_probe_task, "wifi_probe", 8192, nullptr, 5, nullptr, 0);
    xTaskCreatePinnedToCore(ble_task,        "ble_ghost",  8192, nullptr, 4, nullptr, 1);

    ESP_LOGI(TAG, "Running. Free heap after init: %lu bytes", esp_get_free_heap_size());
}

void loop() {
    static uint32_t last_ms = 0;
    if (millis() - last_ms >= 30000) {
        ESP_LOGI(TAG, "Heap: %lu bytes | Uptime: %lus",
                 esp_get_free_heap_size(), millis() / 1000UL);
        last_ms = millis();
    }
    delay(1000);
}
