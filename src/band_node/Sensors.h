#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "MAX30105.h" // SparkFun MAX3010x uses this header
#include "heartRate.h"
#include "ResQConfig.h"

// Sensor instances
extern Adafruit_MPU6050 mpu;
extern MAX30105 particleSensor;

// State variables for vitals
extern uint8_t current_hr;
extern uint8_t current_spo2;
extern bool s_fall_detected;
extern bool s_tap_detected;
extern float s_max_g_force;

void init_sensors();
void poll_sensors();
void read_vitals(uint8_t* hr, uint8_t* spo2);
bool check_emergency_triggers(uint8_t* cause, float* max_g);
