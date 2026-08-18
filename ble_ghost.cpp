#include "ble_ghost.h"
#include "device_profiles.h"
#include "BLEDevice.h"
#include "BLEAdvertising.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "ble_ghost";
static BLEAdvertising* pAdv = nullptr;
static bool initialized    = false;

// NimBLE conn_mode constants (BLE_GAP_CONN_MODE_*):
//   0 = NON (non-connectable)  1 = DIR  2 = UND
#define ADV_NONCONN 0

// ── BLE address generation ─────────────────────────────────────────────────────

// Generates a Bluetooth LE Random Static address.
// Bluetooth Core Spec 6.0 §1.3.2.1: top two bits of byte[0] must be 11.
static void random_static_addr(int slot, uint8_t* addr) {
    uint32_t a = esp_random() ^ ((uint32_t)slot * 0x9e3779b9u);
    uint32_t b = esp_random() ^ ((uint32_t)slot * 0x6c62272eu);
    addr[0] = 0xC0 | (a & 0x3f);
    addr[1] = (a >>  8) & 0xff;
    addr[2] = (a >> 16) & 0xff;
    addr[3] = (a >> 24) & 0xff;
    addr[4] =  b        & 0xff;
    addr[5] = (b >>  8) & 0xff;
}

// ── Public API ─────────────────────────────────────────────────────────────────

bool ble_ghost_init() {
    BLEDevice::init("");
    BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    pAdv = BLEDevice::getAdvertising();
    if (!pAdv) {
        ESP_LOGE(TAG, "getAdvertising() returned null");
        return false;
    }
    initialized = true;
    ESP_LOGI(TAG, "BLE ghost ready");
    return true;
}

void ble_ghost_advertise(int device_index) {
    if (!initialized) return;

    // Select device type from weighted distribution
    int roll = device_index % 100;
    int cumulative = 0;
    int dtype = DTYPE_PIXEL;
    for (int i = 0; i < 5; i++) {
        cumulative += DTYPE_WEIGHTS[i];
        if (roll < cumulative) { dtype = i; break; }
    }

    const char* name = DEVICE_NAMES[device_index % NUM_DEVICE_NAMES];

    uint8_t addr[6];
    random_static_addr(device_index, addr);

    pAdv->stop();
    BLEDevice::setOwnAddr(addr);

    BLEAdvertisementData adv;
    adv.setFlags(0x06);     // LE General Discoverable | BR/EDR Not Supported
    adv.setName(name);

    const MfrData& mfr = MFR_DATA_BY_TYPE[dtype];
    if (mfr.data && mfr.len > 0)
        adv.setManufacturerData(String((const char*)mfr.data, mfr.len));

    BLEAdvertisementData scan_rsp;
    scan_rsp.setName(name);

    pAdv->setAdvertisementData(adv);
    pAdv->setScanResponseData(scan_rsp);
    pAdv->setAdvertisementType(ADV_NONCONN);
    pAdv->setMinInterval(0xa0);   // ~100 ms
    pAdv->setMaxInterval(0xf0);   // ~150 ms
    pAdv->start();

    ESP_LOGD(TAG, "[%d] '%s' dtype=%d addr=%02x:%02x:%02x:%02x:%02x:%02x",
             device_index, name, dtype,
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

void ble_ghost_stop() {
    if (pAdv) pAdv->stop();
}
