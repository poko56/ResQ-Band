#include "Calibration.h"
#include <Preferences.h>
#include <math.h>

static float s_default_ref = -75.0f, s_default_n = 2.5f;
static float s_ref = -75.0f, s_n = 2.5f;
static bool  s_is_default = true;

static CalPoint s_points[CAL_MAX_POINTS];
static size_t   s_count = 0;

static struct {
  bool     active;
  uint32_t band_id;
  float    distance_m;
  int16_t  samples[CAL_SAMPLES_PER_POINT];
  uint8_t  n;
  uint32_t started_ms;
} s_cap = {};
static const char* s_last_error = "";

static const char* NVS_NS = "resqcal";

void cal_begin(float default_ref_1m, float default_n) {
  s_default_ref = s_ref = default_ref_1m;
  s_default_n = s_n = default_n;
  s_is_default = true;
  Preferences p;
  if (p.begin(NVS_NS, true)) {
    if (p.isKey("ref") && p.isKey("n")) {
      s_ref = p.getFloat("ref", default_ref_1m);
      s_n = p.getFloat("n", default_n);
      s_is_default = false;
    }
    p.end();
  }
  Serial.printf("[CAL] RSSI@1m=%.1f n=%.2f (%s)\n", s_ref, s_n, s_is_default ? "default" : "calibrated");
}

float cal_ref_1m()     { return s_ref; }
float cal_n()          { return s_n; }
bool  cal_is_default() { return s_is_default; }

bool cal_start(uint32_t band_id, float distance_m, const char** error) {
  const char* err = nullptr;
  if (band_id == 0)                               err = "ยังไม่ได้ล็อกกำไล";
  else if (!(distance_m >= 0.3f && distance_m <= 500.0f)) err = "ระยะต้องอยู่ระหว่าง 0.3–500 ม.";
  else if (s_count >= CAL_MAX_POINTS)             err = "ครบ 10 จุดแล้ว ลบจุดเก่าก่อน";
  else if (s_cap.active)                          err = "กำลังวัดจุดก่อนหน้าอยู่";
  if (err) { if (error) *error = err; return false; }
  s_cap = {};
  s_cap.active = true;
  s_cap.band_id = band_id;
  s_cap.distance_m = distance_m;
  s_cap.started_ms = millis();
  s_last_error = "";
  Serial.printf("[CAL] capture %.1f m from %08X\n", distance_m, (unsigned)band_id);
  return true;
}

static float median_of(int16_t* v, uint8_t n) {
  for (uint8_t i = 1; i < n; i++)
    for (uint8_t j = i; j > 0 && v[j - 1] > v[j]; j--) { int16_t t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
  return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0f;
}

void cal_feed(uint32_t band_id, int16_t rssi) {
  if (!s_cap.active || band_id != s_cap.band_id) return;
  s_cap.samples[s_cap.n++] = rssi;
  if (s_cap.n < CAL_SAMPLES_PER_POINT) return;
  CalPoint& p = s_points[s_count++];
  p.distance_m = s_cap.distance_m;
  p.rssi = median_of(s_cap.samples, s_cap.n);
  p.samples = s_cap.n;
  s_cap.active = false;
  Serial.printf("[CAL] point %u: %.1f m -> %.1f dBm\n", (unsigned)s_count, p.distance_m, p.rssi);
}

void cal_tick() {
  if (!s_cap.active || millis() - s_cap.started_ms < CAL_CAPTURE_TIMEOUT_MS) return;
  s_cap.active = false;
  s_last_error = "ไม่ได้รับสัญญาณกำไลครบภายใน 45 วิ ลองใหม่";
}

bool        cal_capturing()  { return s_cap.active; }
uint8_t     cal_captured()   { return s_cap.n; }
float       cal_capture_distance() { return s_cap.distance_m; }
const char* cal_last_error() { return s_last_error; }

const CalPoint* cal_points(size_t* count) { *count = s_count; return s_points; }
void cal_remove_last() { if (s_count) s_count--; }
void cal_clear()       { s_count = 0; s_cap.active = false; }

CalFit cal_fit() {
  CalFit f = {false, 0, 0, 0, ""};
  if (s_count < 3) { f.error = "ต้องมีอย่างน้อย 3 จุด"; return f; }
  float dmin = s_points[0].distance_m, dmax = dmin;
  float mx = 0, my = 0;
  for (size_t i = 0; i < s_count; i++) {
    dmin = fminf(dmin, s_points[i].distance_m);
    dmax = fmaxf(dmax, s_points[i].distance_m);
    mx += log10f(s_points[i].distance_m);
    my += s_points[i].rssi;
  }
  if (dmax < dmin * 3) { f.error = "ระยะใกล้กันเกินไป ไกลสุดควรอย่างน้อย 3 เท่าของใกล้สุด"; return f; }
  mx /= s_count; my /= s_count;
  float sxy = 0, sxx = 0, syy = 0;
  for (size_t i = 0; i < s_count; i++) {
    const float dx = log10f(s_points[i].distance_m) - mx, dy = s_points[i].rssi - my;
    sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
  }
  const float slope = sxy / sxx;            // = -10 n
  f.n = -slope / 10.0f;
  f.ref_1m = my - slope * mx;
  f.r2 = syy > 0 ? (sxy * sxy) / (sxx * syy) : 1.0f;
  // Physically n sits around 2 (open air) to ~4.5 (heavy obstruction). Far
  // outside that the points are noise, not a distance trend.
  if (f.n < 1.5f || f.n > 5.0f) { f.error = "ค่า n ผิดปกติ: RSSI ไม่ลดลงตามระยะ ลองวัดใหม่ให้ระยะห่างกันมากขึ้น"; return f; }
  f.ok = true;
  return f;
}

CalFit cal_apply() {
  CalFit f = cal_fit();
  if (!f.ok) return f;
  s_ref = f.ref_1m;
  s_n = f.n;
  s_is_default = false;
  Preferences p;
  if (p.begin(NVS_NS, false)) {
    p.putFloat("ref", s_ref);
    p.putFloat("n", s_n);
    p.end();
  }
  Serial.printf("[CAL] saved RSSI@1m=%.1f n=%.2f R2=%.2f\n", f.ref_1m, f.n, f.r2);
  return f;
}

void cal_reset() {
  s_ref = s_default_ref;
  s_n = s_default_n;
  s_is_default = true;
  Preferences p;
  if (p.begin(NVS_NS, false)) { p.clear(); p.end(); }
  Serial.println("[CAL] reset to defaults");
}
