#pragma once
#include <Arduino.h>

// The physical OLED has been dropped: the rescuer's entire UI is the WebUI
// served at http://192.168.4.1 (connect to the "ResQ-Node" WiFi AP). These
// remain as inline no-op stubs so existing call sites compile unchanged.
inline void init_display() {}
inline void display_lora_sweep(uint32_t /*target_id*/, int16_t /*rssi*/, uint8_t /*hr*/, uint8_t /*spo2*/) {}
inline void display_uwb_pinpoint(uint32_t /*target_id*/, float /*distance_m*/, float /*angle_deg*/) {}
