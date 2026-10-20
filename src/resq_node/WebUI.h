#pragma once
#include <Arduino.h>

struct SeenBand {
  uint32_t id;
  int16_t rssi;
  uint8_t hr;
  uint8_t spo2;
  uint32_t last_seen_ms;
};

extern uint32_t webui_wants_target_id;
extern volatile bool webui_wants_ring;   // set by POST /api/ring

enum RingState : uint8_t { RING_IDLE = 0, RING_SENDING = 1, RING_ACKED = 2, RING_TIMEOUT = 3 };
void webui_set_ring_state(RingState s);

extern volatile uint8_t webui_wants_found;   // 0 = none, else FoundOutcome + 1 (POST /api/found)
enum FoundState : uint8_t { FOUND_IDLE = 0, FOUND_SENDING = 1, FOUND_SENT = 2 };
void webui_set_found_state(FoundState s);

void init_web_ui();
void webui_loop();
void webui_set_bands(SeenBand* bands, size_t count);
void webui_set_target(uint32_t id, uint8_t mode, float dist, float angle);
// Vitals of the currently locked wearer (as delivered by the dispatch or heard
// over LoRa) so the WebUI can show who the rescuer is fetching + their state.
void webui_set_vitals(uint8_t hr, uint8_t spo2, uint8_t batt, uint8_t triage);
