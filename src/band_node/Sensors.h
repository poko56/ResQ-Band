#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "MAX30105.h" // SparkFun MAX3010x uses this header
#include "heartRate.h"
#include "ResQConfig.h"

// Sensor instances
extern MAX30105 particleSensor;

// State variables for vitals
extern uint8_t current_hr;
extern uint8_t current_spo2;

void init_sensors();
void poll_sensors();
void read_vitals(uint8_t* hr, uint8_t* spo2);
bool sensor_present();        // MAX30102 answered at init (or a later retry)
uint32_t contact_ms();        // how long the sensor has had skin contact, 0 = none
bool check_emergency_triggers(uint8_t* cause, float* max_g);
