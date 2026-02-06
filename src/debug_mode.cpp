#include "debug_mode.h"
#include "log_buffer.h"

namespace debug_mode {

namespace {
    bool g_initialized = false;
    bool g_forced = false;
    bool g_current_state = false;  // true = DEBUG enabled
}

void init() {
    if (g_initialized) return;
    
    // GPIO12 configuration:
    //   - PULL-UP (default HIGH when floating)
    //   - Connect to GND to ENABLE debug mode
    //   - Leave floating to DISABLE debug mode
    
    pinMode(DEBUG_PIN, INPUT_PULLUP);
    
    // Small delay for pin to stabilize
    delay(10);
    
    g_initialized = true;
    
    // Read initial state (LOW = DEBUG ON, HIGH = DEBUG OFF)
    g_current_state = (digitalRead(DEBUG_PIN) == LOW);
    
    LOG_INFO("Debug mode GPIO%d initialized: %s (connect to GND to enable)", 
             DEBUG_PIN, 
             g_current_state ? "ENABLED" : "DISABLED");
}

bool isEnabled() {
    if (g_forced) return true;
    if (!g_initialized) return false;
    return g_current_state;
}

void update() {
    if (!g_initialized) return;
    
    // LOW = DEBUG ON, HIGH = DEBUG OFF
    bool newState = (digitalRead(DEBUG_PIN) == LOW);
    
    // Check if state changed
    if (newState != g_current_state) {
        g_current_state = newState;
        
        // Log state change as INFO (always visible)
        if (g_current_state) {
            LOG_INFO(">>> Debug mode ENABLED (GPIO%d = GND) <<<", DEBUG_PIN);
        } else {
            LOG_INFO(">>> Debug mode DISABLED (GPIO%d = floating) <<<", DEBUG_PIN);
        }
    }
}

void setForced(bool enable) {
    bool wasEnabled = isEnabled();
    g_forced = enable;
    bool nowEnabled = isEnabled();
    
    if (wasEnabled != nowEnabled) {
        LOG_INFO("Debug mode %s via software override", 
                 nowEnabled ? "FORCE ENABLED" : "UNFORCED");
    }
}

bool isForced() {
    return g_forced;
}

} // namespace debug_mode
