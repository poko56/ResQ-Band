#include "BLE_Scanner.h"
#include <NimBLEDevice.h>

// Shared with the NimBLE scan task; simple 32-bit fields are atomic enough on
// ESP32 for a proximity read-out.
static volatile uint32_t s_target_id = 0;   // 0 = any ResQ-Band
static volatile int      s_rssi_ema  = 0;
static volatile uint32_t s_last_seen = 0;
static volatile bool     s_have      = false;

// Band beacon manufacturer payload = [0xFFFF][device_id little-endian].
static bool parseResQ(const std::string& mfg, uint32_t* id) {
  if (mfg.size() < 6) return false;
  if ((uint8_t)mfg[0] != 0xFF || (uint8_t)mfg[1] != 0xFF) return false;
  *id =  (uint32_t)(uint8_t)mfg[2]
      | ((uint32_t)(uint8_t)mfg[3] << 8)
      | ((uint32_t)(uint8_t)mfg[4] << 16)
      | ((uint32_t)(uint8_t)mfg[5] << 24);
  return true;
}

class ScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice* dev) override {
    if (!dev->haveManufacturerData()) return;
    uint32_t id;
    if (!parseResQ(dev->getManufacturerData(), &id)) return;
    if (s_target_id != 0 && id != s_target_id) return;   // not our target

    int rssi = dev->getRSSI();
    if (!s_have) { s_rssi_ema = rssi; s_have = true; }
    else         { s_rssi_ema = (s_rssi_ema * 3 + rssi) / 4; }  // EMA alpha=1/4
    s_last_seen = millis();
  }
};

void init_ble_scanner() {
  static bool inited = false;
  if (inited) return;
  inited = true;

  NimBLEDevice::init("ResQ-Node");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new ScanCallbacks(), /*wantDuplicates*/ true);
  scan->setActiveScan(false);   // passive: lighter, coexists with the WiFi AP
  scan->setInterval(160);       // 160 * 0.625 ms = 100 ms
  scan->setWindow(48);          // 48 * 0.625 ms = 30 ms  (~30% duty, WiFi headroom)
  scan->setMaxResults(0);       // callback-only, don't buffer results
  scan->start(0, nullptr, false);   // scan forever
  Serial.println("[BLE] scanner started (passive 30ms/100ms)");
}

void ble_set_target(uint32_t device_id) {
  s_target_id = device_id;
  s_have = false;               // reset the smoother for the new target
}

bool ble_get_target(int* rssi_smoothed, uint32_t* age_ms) {
  if (!s_have) return false;
  *rssi_smoothed = s_rssi_ema;
  *age_ms = millis() - s_last_seen;
  return true;
}
