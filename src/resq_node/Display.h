#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "ResQConfig.h"

void init_display();
void display_lora_sweep(uint32_t target_id, int16_t rssi, uint8_t hr, uint8_t spo2);
void display_uwb_pinpoint(uint32_t target_id, float distance_m, float angle_deg);
