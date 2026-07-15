#pragma once
#include <Arduino.h>
#include "ResQConfig.h"

void init_uwb_initiator();
void poll_uwb_initiator(uint32_t target_id, float* distance_m, float* angle_deg);
