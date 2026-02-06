#include "led_status.h"
#include "log_buffer.h"

// FreeRTOS headers
extern "C" {
    #include "freertos/FreeRTOS.h"
    #include "freertos/task.h"
}

namespace led_status {

namespace {
    // Current state
    volatile State g_current_state = State::STANDBY;
    
    // Task handle for LED control
    TaskHandle_t g_led_task_handle = nullptr;
    
    // Flag to track WiFi override
    bool g_wifi_override = false;
    State g_saved_state = State::STANDBY;
    
    // Configuration
    LedConfig g_config;
    
    /**
     * @brief Set LED output on configured pins
     */
    void setLedOutput(bool on) {
        if (g_config.useBuiltinLed) {
            digitalWrite(LED_BUILTIN_PIN, on ? HIGH : LOW);
        }
        if (g_config.useExternalLed) {
            digitalWrite(LED_EXTERNAL_PIN, on ? HIGH : LOW);
        }
    }
    
    /**
     * @brief Execute a pattern of N pulses followed by a pause
     * @param pulses Number of pulses to emit
     */
    void playPulsePattern(int pulses) {
        for (int i = 0; i < pulses; i++) {
            setLedOutput(true);
            vTaskDelay(pdMS_TO_TICKS(PULSE_ON_MS));
            setLedOutput(false);
            
            // Gap between pulses (except after last)
            if (i < pulses - 1) {
                vTaskDelay(pdMS_TO_TICKS(PULSE_OFF_MS));
            }
        }
        // Pause after pattern
        vTaskDelay(pdMS_TO_TICKS(PATTERN_PAUSE_MS));
    }
    
    /**
     * @brief LED control task - runs continuously
     * If LED stops blinking, system is frozen!
     */
    void ledTask(void* parameter) {
        while (true) {
            State currentState = g_current_state;
            
            switch (currentState) {
                case State::STANDBY:
                    // 1Hz blink (0.5s on, 0.5s off)
                    setLedOutput(true);
                    vTaskDelay(pdMS_TO_TICKS(STANDBY_PERIOD_MS / 2));
                    setLedOutput(false);
                    vTaskDelay(pdMS_TO_TICKS(STANDBY_PERIOD_MS / 2));
                    break;
                    
                case State::WIFI_DISCONNECTED:
                    // 2 pulses + 2s pause
                    playPulsePattern(2);
                    break;
                    
                case State::READING_MOSFET:
                    // 3 pulses + 2s pause (NEW pattern for ADC/DAC active)
                    playPulsePattern(3);
                    break;
                    
                case State::MEASURING:
                    // Frenetic blink every 0.1s (file writing)
                    setLedOutput(true);
                    vTaskDelay(pdMS_TO_TICKS(RECORDING_PERIOD_MS / 2));
                    setLedOutput(false);
                    vTaskDelay(pdMS_TO_TICKS(RECORDING_PERIOD_MS / 2));
                    break;
            }
        }
    }
    
} // anonymous namespace

void init(const LedConfig& config) {
    g_config = config;
    
    // Configure LED pins
    if (config.useBuiltinLed) {
        pinMode(LED_BUILTIN_PIN, OUTPUT);
        digitalWrite(LED_BUILTIN_PIN, LOW);
    }
    
    if (config.useExternalLed) {
        pinMode(LED_EXTERNAL_PIN, OUTPUT);
        digitalWrite(LED_EXTERNAL_PIN, LOW);
    }
    
    // Create LED control task
    BaseType_t result = xTaskCreatePinnedToCore(
        ledTask,
        "LedStatusTask",
        2048,           // Stack size
        nullptr,        // Parameters
        1,              // Priority (low)
        &g_led_task_handle,
        0               // Core 0 (leaving Core 1 for main tasks)
    );
    
    if (result == pdPASS) {
        LOG_INFO("LED Status v2.0 initialized");
        if (config.useBuiltinLed) LOG_INFO("  Built-in LED: GPIO%d", LED_BUILTIN_PIN);
        if (config.useExternalLed) LOG_INFO("  External LED: GPIO%d", LED_EXTERNAL_PIN);
    } else {
        LOG_ERROR("Failed to create LED status task");
    }
}

void setState(State newState) {
    if (g_current_state != newState) {
        g_current_state = newState;
        LOG_DEBUG("LED state changed to: %s", getStateName(newState));
    }
}

State getState() {
    return g_current_state;
}

const char* getStateName(State state) {
    switch (state) {
        case State::STANDBY: return "STANDBY";
        case State::WIFI_DISCONNECTED: return "WIFI_DISCONNECTED";
        case State::READING_MOSFET: return "READING_MOSFET";
        case State::MEASURING: return "MEASURING";
        default: return "UNKNOWN";
    }
}

void updateWiFiStatus(bool isConnected) {
    if (!isConnected) {
        // WiFi lost - save current state and switch to disconnected pattern
        if (g_current_state != State::WIFI_DISCONNECTED) {
            g_saved_state = g_current_state;
            g_wifi_override = true;
            setState(State::WIFI_DISCONNECTED);
        }
    } else {
        // WiFi restored - return to previous state
        if (g_wifi_override) {
            g_wifi_override = false;
            setState(g_saved_state);
        }
    }
}

} // namespace led_status
