#pragma once
#include <stdint.h>

// Initialize Wi-Fi for raw 802.11 frame injection.
// Must be called once before wifi_send_probe_request().
bool wifi_inject_init();

// Inject a single 802.11 probe request frame.
//   src_mac  — 6-byte source MAC address
//   ssid     — network name to probe for; NULL or ssid_len=0 sends a wildcard probe
//   ssid_len — byte length of ssid (max 32)
//   channel  — 2.4 GHz channel to transmit on (1–13)
bool wifi_send_probe_request(const uint8_t* src_mac,
                             const char*    ssid,
                             uint8_t        ssid_len,
                             uint8_t        channel);

// Switch the radio to a different channel.
void wifi_set_channel(uint8_t channel);
