#pragma once
#include <stdint.h>

// Initialize the BLE stack for ghost advertisements.
// Must be called once before ble_ghost_advertise().
bool ble_ghost_init();

// Advertise as the virtual device at device_index.
// Generates a fresh random static BLE address, sets the device name and
// manufacturer-specific data appropriate for that device's type, then starts
// a non-connectable advertisement.
void ble_ghost_advertise(int device_index);

// Stop the current BLE advertisement.
void ble_ghost_stop();
