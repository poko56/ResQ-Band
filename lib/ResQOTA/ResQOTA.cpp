#include "ResQOTA.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>

namespace ResQOTA {

// ============================================================================
// WiFi
// ============================================================================
bool ensure_wifi(const char* ssid, const char* pass, uint32_t timeout_ms) {
  if (!ssid || !*ssid) return false;
  if (WiFi.status() == WL_CONNECTED) return true;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  const uint32_t deadline = millis() + timeout_ms;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(200);
  }
  return WiFi.status() == WL_CONNECTED;
}

// ============================================================================
// is_newer  (semver-ish)
// ----------------------------------------------------------------------------
// Accepts "v0.3.0", "0.3.0", "0.3", "0.3.0-beta" (suffix ignored). Returns
// true iff `remote` strictly beats `current` component-by-component. Junk
// components are treated as 0.
// ============================================================================
static uint32_t parse_component(const char*& p) {
  uint32_t n = 0;
  while (*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); ++p; }
  return n;
}

bool is_newer(const char* remote_tag, const char* current_fw) {
  if (!remote_tag || !current_fw) return false;
  const char* r = (*remote_tag  == 'v') ? remote_tag  + 1 : remote_tag;
  const char* c = (*current_fw == 'v') ? current_fw + 1 : current_fw;

  for (uint8_t i = 0; i < 3; ++i) {
    const uint32_t rv = parse_component(r);
    const uint32_t cv = parse_component(c);
    if (rv > cv) return true;
    if (rv < cv) return false;
    if (*r == '.') ++r;
    if (*c == '.') ++c;
  }
  return false;
}

// ============================================================================
// check_latest
// ============================================================================
LatestRelease check_latest(const char* repo_owner,
                           const char* repo_name,
                           const char* binary_name) {
  LatestRelease out{};
  if (WiFi.status() != WL_CONNECTED) return out;

  WiFiClientSecure client;
  client.setInsecure();   // TODO: pin GitHub cert in production builds
  client.setTimeout(8000);

  HTTPClient http;
  http.setReuse(false);
  http.setUserAgent("ResQ-Band-OTA");

  String url = "https://api.github.com/repos/";
  url += repo_owner; url += "/"; url += repo_name; url += "/releases/latest";

  if (!http.begin(client, url)) return out;
  http.addHeader("Accept", "application/vnd.github+json");

  const int code = http.GET();
  if (code != 200) {
    Serial.printf("[OTA] GitHub API HTTP %d\n", code);
    http.end();
    return out;
  }

  // Keep memory small: only parse the fields we need
  JsonDocument filter;
  filter["tag_name"] = true;
  JsonObject assetf = filter["assets"][0].to<JsonObject>();
  assetf["name"] = true;
  assetf["browser_download_url"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream(),
                                             DeserializationOption::Filter(filter));
  http.end();
  if (err) {
    Serial.printf("[OTA] JSON parse: %s\n", err.c_str());
    return out;
  }

  out.tag_name = String((const char*)doc["tag_name"]);

  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    const char* name = asset["name"];
    const char* dl   = asset["browser_download_url"];
    if (!name || !dl) continue;
    if (strcmp(name, binary_name) == 0) {
      out.binary_url = String(dl);
    } else {
      String suffix = String(binary_name) + ".md5";
      if (strcmp(name, suffix.c_str()) == 0) out.md5_url = String(dl);
    }
  }

  out.ok = out.binary_url.length() > 0;
  return out;
}

// ============================================================================
// perform_update
// ============================================================================
UpdateResult perform_update(const String& url) {
  if (WiFi.status() != WL_CONNECTED) return UPDATE_NO_WIFI;
  if (url.length() == 0)            return UPDATE_HTTP_FAIL;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  httpUpdate.rebootOnUpdate(true);
  httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

  const t_httpUpdate_return ret = httpUpdate.update(client, url);
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("[OTA] update failed: (%d) %s\n",
                    httpUpdate.getLastError(),
                    httpUpdate.getLastErrorString().c_str());
      return UPDATE_HTTP_FAIL;
    case HTTP_UPDATE_NO_UPDATES:
      return UPDATE_NO_NEW;
    case HTTP_UPDATE_OK:
      // Unreachable: rebootOnUpdate(true) restarts the chip first
      return UPDATE_OK;
  }
  return UPDATE_HTTP_FAIL;
}

// ============================================================================
// run_once - the convenience path most firmwares call
// ============================================================================
UpdateResult run_once(const char* ssid,
                      const char* pass,
                      const char* repo_owner,
                      const char* repo_name,
                      const char* binary_name,
                      const char* current_fw,
                      uint32_t wifi_timeout_ms) {
  if (!ensure_wifi(ssid, pass, wifi_timeout_ms)) {
    Serial.println("[OTA] WiFi unavailable - skipping check");
    return UPDATE_NO_WIFI;
  }
  Serial.printf("[OTA] WiFi up (rssi %d) - checking GitHub...\n", WiFi.RSSI());

  LatestRelease latest = check_latest(repo_owner, repo_name, binary_name);
  if (!latest.ok) {
    Serial.println("[OTA] no matching asset in latest release");
    return UPDATE_HTTP_FAIL;
  }

  Serial.printf("[OTA] current=%s remote=%s\n", current_fw, latest.tag_name.c_str());
  if (!is_newer(latest.tag_name.c_str(), current_fw)) {
    Serial.println("[OTA] already up to date");
    return UPDATE_NO_NEW;
  }

  Serial.printf("[OTA] update available: %s\n", latest.binary_url.c_str());
  Serial.println("[OTA] flashing... chip will reboot on success");
  return perform_update(latest.binary_url);
}

}  // namespace ResQOTA
