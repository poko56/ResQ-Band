#pragma once

// ============================================================================
// On-device path-loss calibration for the RSSI -> distance estimate.
//
// The rescuer stands at a measured distance from a band and taps "record": the
// next CAL_SAMPLES_PER_POINT heartbeats from the locked band are captured and
// their median becomes one (distance, RSSI) point. With a few points spread
// over 1..30+ m, a least-squares fit of
//     rssi = ref_1m - 10 * n * log10(d)
// gives the two constants, which are saved to NVS and survive reboots.
// ============================================================================

#include <Arduino.h>

#define CAL_MAX_POINTS        10
#define CAL_SAMPLES_PER_POINT 3
#define CAL_CAPTURE_TIMEOUT_MS 45000   // heartbeats arrive every ~10 s

struct CalPoint {
  float   distance_m;
  float   rssi;        // median of the captured samples
  uint8_t samples;
};

struct CalFit {
  bool        ok;
  float       ref_1m;
  float       n;
  float       r2;
  const char* error;   // why ok == false (Thai, shown in the WebUI)
};

void  cal_begin(float default_ref_1m, float default_n);

// Model currently used for distances.
float cal_ref_1m();
float cal_n();
bool  cal_is_default();

// Capture. cal_start begins recording the next samples of `band_id`; feed every
// band packet to cal_feed. Returns false if a point can't be started.
bool  cal_start(uint32_t band_id, float distance_m, const char** error);
void  cal_feed(uint32_t band_id, int16_t rssi);
void  cal_tick();                       // expires a stalled capture
bool  cal_capturing();
uint8_t cal_captured();                 // samples so far in the running capture
float cal_capture_distance();           // distance of the running capture
const char* cal_last_error();           // last capture failure, "" if none

const CalPoint* cal_points(size_t* count);
void  cal_remove_last();
void  cal_clear();

CalFit cal_fit();                       // fit the recorded points (no side effects)
CalFit cal_apply();                     // fit, then use + save it if ok
void  cal_reset();                      // back to the built-in defaults
