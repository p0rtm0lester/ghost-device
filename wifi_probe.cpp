#include "wifi_probe.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char* TAG = "wifi_probe";
static uint16_t seq_num = 0;

// ── 802.11 frame constants ─────────────────────────────────────────────────────

// Probe Request header template (24 bytes).
// Byte offsets: [0-1] FC, [2-3] Duration, [4-9] DA, [10-15] SA, [16-21] BSSID, [22-23] SeqCtrl
static const uint8_t PROBE_HEADER[] = {
    0x40, 0x00,                                     // Frame Control: Probe Request
    0x00, 0x00,                                     // Duration
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             // DA: broadcast
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,             // SA: filled at send time
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,             // BSSID: broadcast
    0x00, 0x00,                                     // Sequence Control: filled at send time
};

// Supported Rates IE (tag 1): 1, 2, 5.5, 11, 6, 9, 12, 18 Mbps
static const uint8_t IE_RATES[] = {
    0x01, 0x08,
    0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24,
};

// Extended Supported Rates IE (tag 50): 24, 36, 48, 54 Mbps
static const uint8_t IE_EXT_RATES[] = {
    0x32, 0x04,
    0x30, 0x48, 0x60, 0x6c,
};

// HT Capabilities IE (tag 45): signals 802.11n support
static const uint8_t IE_HT_CAP[] = {
    0x2d, 0x1a,
    0xad, 0x01,                                     // HT Capabilities Info
    0x17,                                           // A-MPDU Parameters
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Supported MCS Set
    0x00, 0x00,                                     // HT Extended Capabilities
    0x00, 0x00, 0x00, 0x00,                         // Transmit Beamforming
    0x00,                                           // ASEL Capabilities
};

// Extended Capabilities IE (tag 127): common in modern 802.11ac/ax devices
static const uint8_t IE_EXT_CAP[] = {
    0x7f, 0x08,
    0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40,
};

// ── Public API ─────────────────────────────────────────────────────────────────

bool wifi_inject_init() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %d", ret);
        return false;
    }

    ret  = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    ret |= esp_wifi_set_mode(WIFI_MODE_STA);
    ret |= esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi start: %d", ret);
        return false;
    }

    esp_wifi_set_promiscuous(true);  // required for esp_wifi_80211_tx()
    ESP_LOGI(TAG, "Wi-Fi injection ready");
    return true;
}

void wifi_set_channel(uint8_t channel) {
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

bool wifi_send_probe_request(const uint8_t* src_mac,
                             const char*    ssid,
                             uint8_t        ssid_len,
                             uint8_t        channel) {
    // Max frame size: header(24) + SSID_IE(34) + rates(10) + ext_rates(6)
    //                + ds_param(3) + ht_cap(28) + ext_cap(10) = 115 bytes
    uint8_t frame[128];
    int offset = 0;

    memcpy(frame, PROBE_HEADER, sizeof(PROBE_HEADER));
    memcpy(frame + 10, src_mac, 6);                 // source MAC

    seq_num = (seq_num + 1) & 0x0fff;              // 12-bit sequence counter
    frame[22] = (seq_num << 4) & 0xf0;
    frame[23] = (seq_num >> 4) & 0xff;

    offset = sizeof(PROBE_HEADER);

    // SSID IE (tag 0) — empty SSID = wildcard probe
    frame[offset++] = 0x00;
    frame[offset++] = ssid_len;
    if (ssid_len > 0 && ssid != nullptr) {
        memcpy(frame + offset, ssid, ssid_len);
        offset += ssid_len;
    }

    memcpy(frame + offset, IE_RATES,     sizeof(IE_RATES));     offset += sizeof(IE_RATES);
    memcpy(frame + offset, IE_EXT_RATES, sizeof(IE_EXT_RATES)); offset += sizeof(IE_EXT_RATES);

    // DS Parameter Set IE (tag 3): advertises current channel
    frame[offset++] = 0x03;
    frame[offset++] = 0x01;
    frame[offset++] = channel;

    memcpy(frame + offset, IE_HT_CAP,  sizeof(IE_HT_CAP));  offset += sizeof(IE_HT_CAP);
    memcpy(frame + offset, IE_EXT_CAP, sizeof(IE_EXT_CAP)); offset += sizeof(IE_EXT_CAP);

    wifi_set_channel(channel);

    esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, frame, offset, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tx failed: %d", err);
        return false;
    }
    return true;
}
