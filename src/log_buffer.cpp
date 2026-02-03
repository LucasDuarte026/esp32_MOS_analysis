#include "log_buffer.h"

// Global instance
LogBuffer g_log_buffer;

// ============================================================================
// Debug Mode via GPIO12
// ============================================================================
static bool g_debug_mode_initialized = false;
static bool g_debug_mode_cached = false;
static unsigned long g_debug_last_check = 0;

void initDebugModePin() {
    pinMode(DEBUG_MODE_PIN, INPUT_PULLUP);
    g_debug_mode_initialized = true;
    
    // Initial read - debug enabled when pin is LOW (connected to GND)
    g_debug_mode_cached = (digitalRead(DEBUG_MODE_PIN) == LOW);
    
    if (g_debug_mode_cached) {
        Serial.println("[SYSTEM] Debug mode ENABLED via GPIO12 (pulled LOW)");
    } else {
        Serial.println("[SYSTEM] Debug mode OFF in pin GPIO12 - put the port in LOW to enable");
    }
}

bool isDebugModeEnabled() {
    if (!g_debug_mode_initialized) {
        return false;
    }
    
    // Check every 100ms for responsive state changes
    unsigned long now = millis();
    if (now - g_debug_last_check > 100) {
        // Debug enabled when pin is LOW (connected to GND)
        bool newState = (digitalRead(DEBUG_MODE_PIN) == LOW);
        
        // Detect state change and log it
        if (newState != g_debug_mode_cached) {
            g_debug_mode_cached = newState;
            if (newState) {
                Serial.println("[SYSTEM] Debug mode ENABLED (GPIO12 -> LOW)");
            } else {
                Serial.println("[SYSTEM] Debug mode DISABLED (GPIO12 -> HIGH)");
            }
        }
        
        g_debug_last_check = now;
    }
    
    return g_debug_mode_cached;
}

LogBuffer::LogBuffer() {
    mutex_ = xSemaphoreCreateMutex();
}

void LogBuffer::addLog(LogLevel level, const char* format, va_list args) {
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    addLog(level, String(buffer));
}

void LogBuffer::addLog(LogLevel level, const String& message) {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return;

    LogEntry entry;
    entry.timestamp_ms = millis();
    entry.level = level;
    entry.message = message;

    if (buffer_full_) {
        // Overwrite oldest entry
        logs_[write_index_] = entry;
        write_index_ = (write_index_ + 1) % MAX_LOGS;
    } else {
        logs_.push_back(entry);
        if (logs_.size() >= MAX_LOGS) {
            buffer_full_ = true;
            write_index_ = 0;
        }
    }
    xSemaphoreGive(mutex_);
}

String LogBuffer::getLogsJSON() const {
    // Note: Mutex is mutable or use const_cast? SemaphoreHandle is a pointer, so it's fine.
    // But xSemaphoreTake expects non-const handle. MEMBER is not const, so it's fine.
    // Wait, getLogsJSON is const. accessing mutex_ is fine. Calling xSemaphoreTake changes mutex state? Yes.
    // FreeRTOS mutex logic is internal.
    // Ideally mutex_ should be mutable or we cast away constness locally.
    // Or just make mutex_ mutable in header. I didn't. 
    // I will use xSemaphoreTake((SemaphoreHandle_t)mutex_, ...) or remove const from signature?
    // Changing signature in cpp needs change in header.
    // I'll cast it.
    
    if (!mutex_) return "[]";
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return "[]";

    String json = "[";
    
    size_t count = logs_.size();
    for (size_t i = 0; i < count; i++) {
        // Read in chronological order (oldest first)
        size_t index;
        if (buffer_full_) {
            index = (write_index_ + i) % MAX_LOGS;
        } else {
            index = i;
        }
        
        const LogEntry& entry = logs_[index];
        
        if (i > 0) json += ",";
        
        json += "{";
        json += "\"timestamp\":" + String(entry.timestamp_ms) + ",";
        json += "\"level\":";
        
        switch (entry.level) {
            case LOG_LEVEL_DEBUG: json += "\"debug\""; break;
            case LOG_LEVEL_INFO:  json += "\"info\""; break;
            case LOG_LEVEL_WARN:  json += "\"warn\""; break;
            case LOG_LEVEL_ERROR: json += "\"error\""; break;
        }
        
        json += ",\"message\":\"";
        // Escape quotes in message
        String escaped_msg = entry.message;
        escaped_msg.replace("\"", "\\\"");
        json += escaped_msg;
        json += "\"}";
    }
    
    json += "]";
    xSemaphoreGive(mutex_);
    return json;
}

// Restore function signature
void LogBuffer::clear() {
    if (!mutex_) return;
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        logs_.clear();
        write_index_ = 0;
        buffer_full_ = false;
        xSemaphoreGive(mutex_);
    }
}

// Async Logging Implementation
static QueueHandle_t g_log_queue = nullptr;

static void logTask(void* param) {
    char* msg = nullptr;
    while (true) {
        if (xQueueReceive(g_log_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg) {
                // Serial.println uses mutex internally but is slow.
                Serial.println(msg); 
                free(msg);
            }
        }
    }
}

void initAsyncLogging() {
    g_log_queue = xQueueCreate(64, sizeof(char*)); // Depth 64 messages
    if (g_log_queue) {
        // High priority to drain buffer? No, low priority to not block critical tasks?
        // Serial printing is slow. Low priority is good, let it happen in background.
        // Core 1 shared with main loop which is usually empty (loop()). 
        // Measurement task is on Core 1 too.
        // We want logging to NOT block Measurement task.
        xTaskCreatePinnedToCore(logTask, "LogTask", 3072, NULL, 0, NULL, 1);
        Serial.println("[SYSTEM] Async Logging Initialized");
    } else {
        Serial.println("[ERROR] Failed to create Log Queue");
    }
}

void logToSerialAsync(const char* msg) {
    if (!g_log_queue) {
        Serial.println(msg); // Fallback
        return;
    }
    
    char* copy = strdup(msg);
    if (!copy) return; 
    
    // Don't block indefinitely
    if (xQueueSend(g_log_queue, &copy, pdMS_TO_TICKS(5)) != pdTRUE) {
        free(copy); // Drop log if queue full
    }
}
