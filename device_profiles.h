#pragma once
#include <stdint.h>

// ── Device type identifiers ────────────────────────────────────────────────────

#define DTYPE_IPHONE    0
#define DTYPE_ANDROID   1
#define DTYPE_MACBOOK   2
#define DTYPE_WINDOWS   3
#define DTYPE_PIXEL     4

// ── BLE device names ───────────────────────────────────────────────────────────

const char* const DEVICE_NAMES[] = {
    // Apple phones
    "iPhone", "iPhone 16", "iPhone 16 Pro", "iPhone 15", "iPhone 15 Pro",
    "iPhone 14", "iPhone 14 Pro", "iPhone 13", "iPhone SE",
    // Android phones
    "Galaxy S25", "Galaxy S25+", "Galaxy S24", "Galaxy A55",
    "Pixel 9", "Pixel 9 Pro", "Pixel 8", "Pixel 8 Pro",
    "OnePlus 13", "Moto G Power", "Galaxy S23",
    // MacBooks / iPads
    "MacBook Pro", "MacBook Air", "MacBook Pro (2)", "MacBook Air (2)",
    "iPad Pro", "iPad Air", "iPad",
    // Windows laptops
    "LAPTOP-A7KF2", "DESKTOP-QX3M1", "HP-ENVY-15", "THINKPAD-X1",
    "SURFACE-PRO-11", "DELL-XPS-15", "MSI-LAPTOP", "ASUS-VIVOBOOK",
    // Audio / misc
    "AirPods Pro", "Bose QC45", "Sony WH-1000XM5", "Galaxy Buds2 Pro",
};
const int NUM_DEVICE_NAMES = sizeof(DEVICE_NAMES) / sizeof(DEVICE_NAMES[0]);

// ── Manufacturer-specific data ─────────────────────────────────────────────────
// BLE AD type 0xFF — first two bytes are the company ID (little-endian).

// Apple Inc. (0x004C) — "Nearby Info" beacon, common on all Apple devices
const uint8_t APPLE_MFR_DATA[] = {
    0x4c, 0x00,   // Company ID: Apple Inc.
    0x0c, 0x01,   // Nearby Info type, length 1
    0x00,         // flags
};
const int APPLE_MFR_DATA_LEN = sizeof(APPLE_MFR_DATA);

// Samsung Electronics (0x0075)
const uint8_t SAMSUNG_MFR_DATA[] = {
    0x75, 0x00,   // Company ID: Samsung Electronics
    0x42, 0x09,
    0x00, 0x00, 0x00,
};
const int SAMSUNG_MFR_DATA_LEN = sizeof(SAMSUNG_MFR_DATA);

// Microsoft Corporation (0x0006) — Swift Pair beacon
const uint8_t MSFT_MFR_DATA[] = {
    0x06, 0x00,   // Company ID: Microsoft
    0x03, 0x00,
    0x80,
};
const int MSFT_MFR_DATA_LEN = sizeof(MSFT_MFR_DATA);

// ── Type → manufacturer data map ──────────────────────────────────────────────

struct MfrData { const uint8_t* data; int len; };

const MfrData MFR_DATA_BY_TYPE[] = {
    { APPLE_MFR_DATA,   APPLE_MFR_DATA_LEN   },  // DTYPE_IPHONE
    { SAMSUNG_MFR_DATA, SAMSUNG_MFR_DATA_LEN },  // DTYPE_ANDROID
    { APPLE_MFR_DATA,   APPLE_MFR_DATA_LEN   },  // DTYPE_MACBOOK
    { MSFT_MFR_DATA,    MSFT_MFR_DATA_LEN    },  // DTYPE_WINDOWS
    { nullptr, 0 },                               // DTYPE_PIXEL (no mfr data)
};

// ── Device type distribution ───────────────────────────────────────────────────
// Weights must sum to 100. Reflects a realistic urban device mix.

const uint8_t DTYPE_WEIGHTS[] = {
    35,   // DTYPE_IPHONE   (35%)
    30,   // DTYPE_ANDROID  (30%)
    10,   // DTYPE_MACBOOK  (10%)
    15,   // DTYPE_WINDOWS  (15%)
    10,   // DTYPE_PIXEL    (10%)
};
