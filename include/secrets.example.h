#pragma once
// Copy this file to `include/secrets.h` and fill in your WiFi credentials.
// The real file is gitignored so credentials never end up in the repo.
//
// Used by non-Band devices (MainNode, ResQ-Pin, ResQ-Node) for the OTA
// pull from GitHub Releases. Bands stay USB-only.
//
// Leave the defines blank if you want to build without OTA support; the
// firmware will detect an empty SSID and skip the WiFi attempt cleanly.

#define WIFI_SSID     "YourWiFiName"
#define WIFI_PASSWORD "YourWiFiPassword"
