#pragma once

// ============================================================================
// ResQOTA - shared HTTPS-from-GitHub-Releases OTA client
// ----------------------------------------------------------------------------
// Every non-Band device in the fleet uses the same routine:
//
//   1. ensure_wifi()      - non-blocking-ish join; OK if it fails, caller
//                           keeps LoRa running.
//   2. check_latest()     - hit GitHub's releases/latest endpoint, find the
//                           asset whose name matches our binary (e.g.
//                           "main_node.bin"), return its tag + download URL.
//   3. is_newer()         - cheap semver-ish compare so we don't pull the
//                           same .bin twice on every boot.
//   4. perform_update()   - HTTPUpdate.update() against the asset URL; on
//                           success the chip reboots into the new firmware
//                           and this function never returns.
//
// HTTPS uses setInsecure() so we don't have to ship a CA bundle; harden
// later by pinning the GitHub leaf cert or the Let's Encrypt root.
// ============================================================================

#include <Arduino.h>

namespace ResQOTA {

struct LatestRelease {
  bool   ok;
  String tag_name;     // e.g. "v0.3.0"
  String binary_url;   // direct https:// download
  String md5_url;      // optional - "" if no .md5 asset present
};

enum UpdateResult : uint8_t {
  UPDATE_OK            = 0,   // unreachable in practice - we reboot first
  UPDATE_NO_WIFI       = 1,
  UPDATE_HTTP_FAIL     = 2,
  UPDATE_FLASH_FAIL    = 3,
  UPDATE_NO_NEW        = 4,
};

// Connect to WiFi, blocking up to `timeout_ms`. Returns true if joined.
// Subsequent calls with the same SSID are no-ops if already connected.
bool ensure_wifi(const char* ssid, const char* pass, uint32_t timeout_ms);

// Query GitHub releases/latest and locate the asset matching `binary_name`.
// `repo_owner` and `repo_name` come from build flags (see ResQConfig.h).
LatestRelease check_latest(const char* repo_owner,
                           const char* repo_name,
                           const char* binary_name);

// True if `remote_tag` (e.g. "v0.3.0" or "0.3.0") is strictly newer than
// `current_fw` (e.g. FW_VERSION = "0.2.0"). Handles a leading 'v', missing
// patch components, and rejects non-numeric junk by treating it as 0.
bool is_newer(const char* remote_tag, const char* current_fw);

// Pull the .bin from `url` and hand it to HTTPUpdate. On success the chip
// reboots and execution never returns; on failure returns an error code
// and the caller's normal loop resumes.
UpdateResult perform_update(const String& url);

// Convenience one-shot:
//   1. join WiFi (no-op if already joined)
//   2. check_latest()
//   3. compare to FW_VERSION
//   4. perform_update() if newer
// Logs progress to Serial. Returns the outcome so callers can react.
UpdateResult run_once(const char* ssid,
                      const char* pass,
                      const char* repo_owner,
                      const char* repo_name,
                      const char* binary_name,
                      const char* current_fw,
                      uint32_t wifi_timeout_ms = 12000);

}  // namespace ResQOTA
