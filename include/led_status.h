#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// ============================================================================
// LED Status Indicator System
// ============================================================================
// Uses the built-in LED (GPIO2) with distinct blink patterns for
// different system states. If LED stops blinking, system is frozen.
//
// Patterns:
//   - STANDBY:          1Hz continuous blink (0.5s on, 0.5s off)
//   - WIFI_DISCONNECTED: 2 fast pulses + 2s pause
//   - MEASURING:         Frenetic blink every 0.1s (reading + recording)
// ============================================================================

namespace led_status {

// Pin configuration - Built-in LED
constexpr uint8_t LED_STATUS_PIN = 2;

// Timing constants (milliseconds)
constexpr uint32_t PULSE_ON_MS = 100;       // Duration of each pulse
constexpr uint32_t PULSE_OFF_MS = 150;      // Gap between pulses
constexpr uint32_t PATTERN_PAUSE_MS = 2000; // Pause after pattern
constexpr uint32_t STANDBY_PERIOD_MS = 1000; // Standby blink period (1Hz)
constexpr uint32_t RECORDING_PERIOD_MS = 100; // Frenetic blink period

/**
 * @brief System states that control LED patterns
 */
enum class State {
    STANDBY,            // Normal operation - 1Hz blink (1s cycle)
    WIFI_DISCONNECTED,  // 2 pulses + 2s pause
    MEASURING           // Frenetic blink every 0.1s (ADC/DAC + file writing)
};

/**
 * @brief Initialize the LED status system
 * Must be called once during setup()
 */
void init();

/**
 * @brief Set the current LED state/pattern
 * @param newState The new state to display
 */
void setState(State newState);

/**
 * @brief Get the current LED state
 * @return Current State enum value
 */
State getState();

/**
 * @brief Check if WiFi is connected and update LED state accordingly
 * Should be called periodically from main loop or monitoring task
 */
void updateWiFiStatus(bool isConnected);

} // namespace led_status

#endif // LED_STATUS_H
