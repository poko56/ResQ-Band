#pragma once
#include <Arduino.h>

// Passive BLE scanner for the ResQ-Band last-metre beacon. Runs the NimBLE
// scan continuously in the background and exposes a smoothed RSSI for the
// current target so the handheld can give "hot/cold" proximity guidance.
void init_ble_scanner();
void ble_set_target(uint32_t device_id);            // 0 = accept any ResQ-Band
bool ble_get_target(int* rssi_smoothed, uint32_t* age_ms);
