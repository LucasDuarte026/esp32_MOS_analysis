#ifndef DEBUG_MODE_H
#define DEBUG_MODE_H

#include <Arduino.h>

// ============================================================================
// Debug Mode Controller
// ============================================================================
// Uses GPIO12 as a debug mode trigger with internal pull-up.
// Connect to GND to ENABLE debug mode (verbose logging).
// Leave floating to DISABLE debug mode.
//
// Hardware:
//   - GPIO12 with internal pull-up resistor
//   - Connect GPIO12 to GND to enable debug
// ============================================================================

namespace debug_mode {

// Pin configuration
constexpr uint8_t DEBUG_PIN = 12;  // Pull-down, HIGH = DEBUG mode enabled

/**
 * @brief Initialize the debug mode system
 * Configures GPIO12 with internal pull-down
 */
void init();

/**
 * @brief Check if debug mode is currently enabled
 * @return true if GPIO12 reads HIGH
 */
bool isEnabled();

/**
 * @brief Update debug mode state (call periodically)
 * Updates internal log level based on pin state
 */
void update();

/**
 * @brief Force debug mode on/off (software override)
 * @param enable true to enable, false to disable
 */
void setForced(bool enable);

/**
 * @brief Check if debug mode was forced via software
 */
bool isForced();

} // namespace debug_mode

#endif // DEBUG_MODE_H
