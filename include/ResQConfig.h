#pragma once

#define LORA_FREQUENCY         433E6
#define LORA_SPREADING_FACTOR  9
#define LORA_BANDWIDTH         125E3
#define LORA_CODING_RATE       5
#define LORA_TX_POWER_DBM      20
#define LORA_SYNC_WORD         0xA5

#define HEARTBEAT_INTERVAL_MS  10000
#define SOS_RETRY_INTERVAL_MS  3000

#if defined(DEVICE_TYPE_WRISTBAND)
  #define BOARD_NAME      "ResQ-Wristband"
  #define DEVICE_TYPE_ID  0x01

  #define PIN_LORA_SCK    4
  #define PIN_LORA_MISO   5
  #define PIN_LORA_MOSI   6
  #define PIN_LORA_SS     7
  #define PIN_LORA_RST    8
  #define PIN_LORA_DIO0   9

  #define PIN_UWB_SS      10

  #define PIN_I2C_SDA     0
  #define PIN_I2C_SCL     1

  #define PIN_BUZZER      3
  #define PIN_LED_STATUS  2
  #define PIN_VBAT_ADC    20

#elif defined(DEVICE_TYPE_GATEWAY)
  #define BOARD_NAME      "ResQ-Gateway"
  #define DEVICE_TYPE_ID  0x02

  #define PIN_LORA_SS     5
  #define PIN_LORA_RST    14
  #define PIN_LORA_DIO0   2

  #define PIN_UWB_SS      4
  #define PIN_UWB_IRQ     34
  #define PIN_UWB_RST     27

  #define PIN_LED_STATUS  13

#elif defined(DEVICE_TYPE_HANDHELD)
  #define BOARD_NAME      "ResQ-Handheld"
  #define DEVICE_TYPE_ID  0x03

  #define PIN_LORA_SS     5
  #define PIN_LORA_RST    14
  #define PIN_LORA_DIO0   2

  #define PIN_UWB_SS      4
  #define PIN_UWB_IRQ     34
  #define PIN_UWB_RST     27

  #define PIN_OLED_SDA    21
  #define PIN_OLED_SCL    22

  #define PIN_VIBRATION   25
  #define PIN_BUZZER      26
  #define PIN_BTN_MODE    32
  #define PIN_BTN_LOCK    33
  #define PIN_LED_STATUS  13

#elif defined(DEVICE_TYPE_MAIN_HUB)
  #define BOARD_NAME      "ResQ-MainHub"
  #define DEVICE_TYPE_ID  0x04

  #define PIN_LORA_SS     5
  #define PIN_LORA_RST    14
  #define PIN_LORA_DIO0   2

  #define PIN_LED_STATUS  13
  #define PIN_LED_LORA    12

  #define HUB_WS_PORT     81
  #define HUB_HTTP_PORT   80
  #define HUB_HEARTBEAT_MS 5000

#else
  #error "DEVICE_TYPE_* not defined - set DEVICE_TYPE_WRISTBAND, DEVICE_TYPE_GATEWAY, DEVICE_TYPE_HANDHELD or DEVICE_TYPE_MAIN_HUB"
#endif

#ifndef FW_VERSION
  #define FW_VERSION "0.0.0-dev"
#endif
