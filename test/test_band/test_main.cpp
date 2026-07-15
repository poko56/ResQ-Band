#include <Arduino.h>
#include <unity.h>

// Include your local headers if needed, e.g. #include "Sensors.h"

void setUp(void) {
    // set stuff up here, e.g. initializing pins or Wire
}

void tearDown(void) {
    // clean stuff up here
}

void test_sensor_initialization(void) {
    // Test your hardware initialization. 
    // e.g. init_sensors();
    TEST_ASSERT_TRUE(true);
}

void setup() {
    // Wait for serial monitor to connect
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_sensor_initialization);
    UNITY_END();
}

void loop() {
    delay(100);
}
