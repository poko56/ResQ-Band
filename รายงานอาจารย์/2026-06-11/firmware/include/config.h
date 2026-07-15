// pin map ของแต่ละบอร์ด เลือกอันที่ build ผ่าน build_flag
#pragma once

#define LORA_FREQ      433E6
#define LORA_SF        9

#ifdef BOARD_BAND_NODE
  // ESP32-C3 - กำไลข้อมือ
  #define PIN_LED         2
  #define PIN_BUZZER      3
  #define PIN_SDA         0
  #define PIN_SCL         1
  #define PIN_LORA_SCK    4
  #define PIN_LORA_MISO   5
  #define PIN_LORA_MOSI   6
  #define PIN_LORA_CS     7
  #define PIN_LORA_RST    8
  #define PIN_LORA_DIO0   9
  #define PIN_VBAT        20
#endif

#ifdef BOARD_MAIN_NODE
  // ESP32-S3 - ศูนย์ควบคุม
  #define PIN_LED         48
  #define PIN_LORA_SCK    12
  #define PIN_LORA_MISO   13
  #define PIN_LORA_MOSI   11
  #define PIN_LORA_CS     10
  #define PIN_LORA_RST    5
  #define PIN_LORA_DIO0   4
#endif

#ifdef BOARD_PILLAR
  // ESP32 - เสาสัญญาณ
  #define PIN_LED         13
  #define PIN_LORA_CS     5
  #define PIN_LORA_RST    14
  #define PIN_LORA_DIO0   2
#endif

#ifdef BOARD_HANDHELD
  // ESP32 - เครื่องพกพาสำหรับกู้ภัย
  #define PIN_LED         13
  #define PIN_LORA_CS     5
  #define PIN_LORA_RST    14
  #define PIN_LORA_DIO0   2
  #define PIN_OLED_SDA    21
  #define PIN_OLED_SCL    22
  #define PIN_BTN         32
#endif
