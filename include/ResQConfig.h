#pragma once

// ============================================================================
// ResQ-Band v2 - Shared compile-time configuration
// ============================================================================
//
// 4 device roles, all share a single LoRa 433 MHz channel via TDMA slotting
// coordinated by the MainNode.
//
//   - Band-Node   (ESP32-C3 + MPU6050 + MAX30102 + LoRa + UWB + Buzzer)
//   - ResQ-Pin    (ESP32     + LoRa)                  -- anchor relay
//   - ResQ-Node   (ESP32     + LoRa + DW3000 + OLED)  -- rescuer handheld
//   - MainNode    (ESP32-S3  + LoRa + USB-CDC)        -- dispatch hub
//
// Pick exactly one of DEVICE_TYPE_* via platformio.ini build_flags.
// ============================================================================

// ---- LoRa radio (433 MHz, ISM band Thailand) -----------------------------
#define LORA_FREQUENCY         433E6
#define LORA_SPREADING_FACTOR  9
#define LORA_BANDWIDTH         125E3
#define LORA_CODING_RATE       5
#define LORA_TX_POWER_DBM      20
#define LORA_SYNC_WORD         0xA5

// ---- TDMA schedule -------------------------------------------------------
// 10s cycle, 10 x 1000ms slots. MainNode publishes a BEACON in slot 0
// so all devices can re-sync their phase to it.
//
//   slot 0       MainNode  -> BEACON (sync + cycle_id + assignment broadcast)
//   slot 1..2    Band #0..1 heartbeat (vitals + battery + status)
//   slot 3..6    Pin  #0..3 sighting reports (RSSI of bands heard)
//   slot 7       MainNode  -> commands / ack
//   slot 8       ResQ-Node -> found-confirm / ring / responses
//   slot 9       EMERGENCY ALOHA window (SOS_TAP, SOS_FALL preempt)
#define TDMA_CYCLE_MS          10000
#define TDMA_SLOT_MS           1000
#define TDMA_NUM_SLOTS         10
#define TDMA_SLOT_BEACON       0
#define TDMA_SLOT_BAND_BASE    1   // band #i uses slot (BASE + i)   i in 0..1
#define TDMA_SLOT_PIN_BASE     3   // pin  #i uses slot (BASE + i)   i in 0..3
#define TDMA_SLOT_MAIN_CMD     7
#define TDMA_SLOT_RESQ_NODE    8
#define TDMA_SLOT_EMERGENCY    9
#define TDMA_MAX_BANDS         2
#define TDMA_MAX_PINS          4

#define HEARTBEAT_INTERVAL_MS  TDMA_CYCLE_MS      // 1 heartbeat per cycle
#define SOS_RETRY_INTERVAL_MS  200                // emergency burst spacing
#define SOS_BURST_COUNT        5                  // repeat SOS this many times

// ---- Triage thresholds (mirror in app/src/lib/triage.ts) ------------------
#define VITAL_HR_CRITICAL_LOW   40
#define VITAL_HR_CRITICAL_HIGH  150
#define VITAL_HR_WARN_LOW       55
#define VITAL_HR_WARN_HIGH      110
#define VITAL_SPO2_CRITICAL     85
#define VITAL_SPO2_WARN         94
#define FALL_G_THRESHOLD_X10    40   // 4.0g in g-force x10
#define BATT_LOW_PCT            10

// ---- Battery (Li-Po 3.7V via 2x100k voltage divider into ADC) -------------
#define VBAT_DIVIDER_RATIO      2.0f
#define VBAT_FULL_MV            4200
#define VBAT_EMPTY_MV           3300

// ============================================================================
// Per-device pin map
// ============================================================================

#if defined(DEVICE_TYPE_BAND_NODE)
  #define BOARD_NAME      "ResQ-BandNode"
  #define DEVICE_TYPE_ID  0x01

  // ESP32-C3 has 1 SPI peripheral - LoRa and UWB share the bus, different CS
  #define PIN_LORA_SCK    4
  #define PIN_LORA_MISO   5
  #define PIN_LORA_MOSI   6
  #define PIN_LORA_SS     7
  #define PIN_LORA_RST    8
  #define PIN_LORA_DIO0   9

  // UWB DW3000 (SPI bus shared with LoRa, separate CS)
  #define PIN_UWB_SS      10
  #define PIN_UWB_IRQ     18
  #define PIN_UWB_RST     19

  // I2C for MPU6050 (accel/gyro) + MAX30102 (HR/SpO2)
  #define PIN_I2C_SDA     0
  #define PIN_I2C_SCL     1
  #define PIN_MPU_INT     21   // tap + free-fall hardware interrupt
  #define PIN_MAX_INT     20   // ALSO used as VBAT_ADC - read-only, mux ok

  // User-facing IO
  #define PIN_BUZZER      3
  #define PIN_LED_STATUS  2
  #define PIN_VBAT_ADC    20   // 2x 100k divider -> ADC1_CH0 on C3

  // Default band index (override via -D BAND_INDEX=1 in env per unit)
  #ifndef BAND_INDEX
    #define BAND_INDEX    0
  #endif

#elif defined(DEVICE_TYPE_RESQ_PIN)
  #define BOARD_NAME      "ResQ-Pin"
  #define DEVICE_TYPE_ID  0x02

  #define PIN_LORA_SS     5
  #define PIN_LORA_RST    14
  #define PIN_LORA_DIO0   2

  #define PIN_LED_STATUS  13
  #define PIN_VBAT_ADC    36   // ADC1_CH0 on classic ESP32

  // Pin index (which physical tower this is). Set via -D PIN_INDEX=2.
  #ifndef PIN_INDEX
    #define PIN_INDEX     0
  #endif

#elif defined(DEVICE_TYPE_RESQ_NODE)
  #define BOARD_NAME      "ResQ-Node"
  #define DEVICE_TYPE_ID  0x03

  #define PIN_LORA_SS     5
  #define PIN_LORA_RST    14
  #define PIN_LORA_DIO0   2

  #define PIN_UWB_SS      4
  #define PIN_UWB_IRQ     34   // input-only - safe for IRQ
  #define PIN_UWB_RST     27

  #define PIN_OLED_SDA    21
  #define PIN_OLED_SCL    22

  #define PIN_VIBRATION   25
  #define PIN_BUZZER      26
  #define PIN_BTN_MODE    32   // cycle mode / next target
  #define PIN_BTN_FOUND   33   // confirm "rescued"
  #define PIN_LED_STATUS  13

#elif defined(DEVICE_TYPE_MAIN_NODE)
  #define BOARD_NAME      "ResQ-MainNode"
  #define DEVICE_TYPE_ID  0x04

  // ESP32-S3 HSPI (SPI2) for LoRa
  #define PIN_LORA_SCK    12
  #define PIN_LORA_MISO   13
  #define PIN_LORA_MOSI   11
  #define PIN_LORA_SS     10
  #define PIN_LORA_RST    5
  #define PIN_LORA_DIO0   4

  #define PIN_LED_STATUS  48   // onboard RGB on most devkitc-1 v1.1
  #define PIN_LED_LORA    47
  #define PIN_BUZZER      17   // local alarm when no ResQ-Node ack within X

  // USB-CDC native (Serial = USB) handled by ARDUINO_USB_CDC_ON_BOOT=1
  #define USB_SERIAL_BAUD     115200
  #define USB_HEARTBEAT_MS    1000
  #define MAIN_NODE_ALARM_MS  10000   // local alarm if no rescuer ack

  // OTA (overridable at build for fork users)
  #ifndef OTA_GITHUB_RELEASE_URL
    #define OTA_GITHUB_RELEASE_URL \
      "https://github.com/poko56/ResQ-Band/releases/latest/download/main_node.bin"
  #endif

#else
  #error "Define one of DEVICE_TYPE_BAND_NODE / RESQ_PIN / RESQ_NODE / MAIN_NODE"
#endif

#ifndef FW_VERSION
  #define FW_VERSION "0.0.0-dev"
#endif
