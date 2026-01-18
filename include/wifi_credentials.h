#pragma once

// ============================================================================
// WiFi Credentials Configuration
// ============================================================================
// Credentials can be defined in one of two ways:
//
// OPTION 1 (Recommended): Create include/secrets.h with your credentials:
//   #define WIFI_SSID "YourSSID"
//   #define WIFI_PASSWORD "YourPassword"
//   #define WIFI_HOSTNAME "esp32-mosfet"
//
// OPTION 2: Define via build flags in platformio.ini:
//   build_flags = 
//     -DWIFI_SSID=\"YourSSID\"
//     -DWIFI_PASSWORD=\"YourPassword\"
//     -DWIFI_HOSTNAME=\"esp32-mosfet\"
// ============================================================================

// Try to include secrets.h if it exists (user's local credentials)
#if __has_include("secrets.h")
  #include "secrets.h"
#endif

// Validate that WIFI_SSID is defined
#ifndef WIFI_SSID
#error "WIFI_SSID must be defined either in include/secrets.h or via build flags in platformio.ini"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""  // Allow open networks
#endif

#ifndef WIFI_HOSTNAME
#define WIFI_HOSTNAME "ESP32-MOSFET-ANALYSIS"
#endif
