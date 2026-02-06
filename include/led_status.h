#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <Arduino.h>

// ============================================================================
// LED Status Indicator System v2.0
// ============================================================================
// Supports both built-in LED (GPIO2) and external LED (GPIO14).
// Distinct blink patterns for different system states.
// If LED stops blinking, system is frozen.
//
// Patterns (v2.0.0):
//   - STANDBY:           1Hz continuous blink (0.5s on, 0.5s off)  
//   - WIFI_DISCONNECTED: 2 fast pulses + 2s pause
//   - READING_MOSFET:    3 fast pulses + 2s pause (ADC/DAC active)
//   - MEASURING:         Frenetic blink every 0.1s (file writing)
// ============================================================================

namespace led_status {

// Pin configuration
constexpr uint8_t LED_BUILTIN_PIN = 2;      // Built-in LED (blue)
constexpr uint8_t LED_EXTERNAL_PIN = 14;    // External LED (green) - NEW

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
    READING_MOSFET,     // 3 pulses + 2s pause (NEW - ADC/DAC reading active)
    MEASURING           // Frenetic blink every 0.1s (file writing)
};

/**
 * @brief LED Configuration
 */
struct LedConfig {
    bool useBuiltinLed = true;   // Use GPIO2 (blue LED)
    bool useExternalLed = true;  // Use GPIO14 (green LED)
};

/**
 * @brief Initialize the LED status system
 * @param config Configuration for which LEDs to use
 */
void init(const LedConfig& config = LedConfig());

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

/**
 * @brief Get state name as string (for logging)
 */
const char* getStateName(State state);

} // namespace led_status

#endif // LED_STATUS_H
