#include <Arduino.h>
#include <unity.h>

// Include your local headers if needed, e.g. #include "Display.h"

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}

void test_display_initialization(void) {
    // Test your hardware initialization. 
    // e.g. init_display();
    TEST_ASSERT_TRUE(true);
}

void setup() {
    // Wait for serial monitor to connect
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_display_initialization);
    UNITY_END();
}

void loop() {
    delay(100);
}
