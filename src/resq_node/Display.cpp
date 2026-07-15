#include "Display.h"
#include <Wire.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void init_display() {
  Serial.println("[Display] Initializing OLED...");
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  if (!display.begin(0x3C, true)) { // 0x3C is common address for 1.3" SH1106
    Serial.println("[Display] SH1106 allocation failed");
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println("ResQ-Node");
  display.println("Initializing...");
  display.display();
}

void display_lora_sweep(uint32_t target_id, int16_t rssi, uint8_t hr, uint8_t spo2) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(" [ LoRa Sweep Mode ]");
  display.println("---------------------");
  if (target_id != 0) {
    display.printf("Target: %08X\n", target_id);
    display.printf("RSSI:   %d dBm\n", rssi);
    display.println();
    display.printf("HR: %u | SpO2: %u%%\n", hr, spo2);
  } else {
    display.println("Scanning for targets...");
  }
  display.display();
}

void display_uwb_pinpoint(uint32_t target_id, float distance_m, float angle_deg) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(" [ UWB Pinpoint ]");
  display.println("---------------------");
  if (target_id != 0) {
    display.printf("Target: %08X\n", target_id);
    display.printf("Dist:   %.2f m\n", distance_m);
    display.printf("Angle:  %.0f deg\n", angle_deg);
  } else {
    display.println("No Target Locked");
  }
  display.display();
}
