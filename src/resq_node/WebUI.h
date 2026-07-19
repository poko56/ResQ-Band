#pragma once
#include <Arduino.h>

struct SeenBand {
  uint32_t id;
  int16_t rssi;
  uint8_t hr;
  uint8_t spo2;
  uint32_t last_seen_ms;
};

extern bool webui_wants_mode_switch;
extern uint32_t webui_wants_target_id;

void init_web_ui();
void webui_loop();
void webui_set_bands(SeenBand* bands, size_t count);
void webui_set_target(uint32_t id, uint8_t mode, float dist, float angle);
// Vitals of the currently locked wearer (as delivered by the dispatch or heard
// over LoRa) so the WebUI can show who the rescuer is fetching + their state.
void webui_set_vitals(uint8_t hr, uint8_t spo2, uint8_t batt, uint8_t triage);
