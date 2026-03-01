#pragma once

#include <stdint.h>

// ============================================================================
// WiFi Manager — Cyclic connection state machine
// ============================================================================
// Protocol:
//   1. Try to connect with compiled-in credentials up to MAX_RETRIES times.
//   2. On failure, scan visible SSIDs and print them over Serial.
//   3. Prompt the user to enter a new SSID and then password via Serial.
//   4. Retry up to MAX_RETRIES with the new credentials.
//   5. Repeat until connected.
// ============================================================================

namespace wifi_manager
{

/// Number of connection attempts before falling back to Serial prompt.
constexpr uint8_t  MAX_RETRIES     = 3;

/// Timeout (ms) for each individual connection attempt.
constexpr uint32_t ATTEMPT_TIMEOUT_MS = 10000;

/// Main entry point — blocks until WiFi is connected.
/// Call this once from setup() in place of the old connectToWifi().
void connectWithFallback();

} // namespace wifi_manager
