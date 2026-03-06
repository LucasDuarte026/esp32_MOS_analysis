#pragma once

// ============================================================================
// Log Buffer — dual-sink logging system (Serial + in-memory ring buffer)
// ============================================================================
// Every LOG_* macro writes to two sinks simultaneously:
//   1. Serial (async, via a FreeRTOS queue to avoid blocking ISRs/tasks)
//   2. g_log_buffer — a 50-entry circular buffer readable at /api/logs
//
// Log level filtering:
//   LOG_DEBUG — only emitted when GPIO12 is pulled LOW (debug jumper)
//   LOG_INFO / LOG_WARN / LOG_ERROR — controlled at compile time via
//   MIN_LOG_LEVEL (0 = all, 3 = errors only). Default is 0.
// ============================================================================

#include <Arduino.h>
#include <vector>

// ----------------------------------------------------------------------------
// Log levels
// ----------------------------------------------------------------------------
enum LogLevel {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
};

// ----------------------------------------------------------------------------
// LogEntry — one entry stored in the circular buffer
// ----------------------------------------------------------------------------
struct LogEntry {
    unsigned long timestamp_ms; ///< millis() at the time the message was logged
    LogLevel      level;
    String        message;
};

// ----------------------------------------------------------------------------
// LogBuffer — 50-entry circular buffer, thread-safe
// ----------------------------------------------------------------------------
class LogBuffer {
private:
    static const size_t MAX_LOGS = 50;
    std::vector<LogEntry> logs_;
    size_t           write_index_ = 0;
    bool             buffer_full_ = false;
    SemaphoreHandle_t mutex_;

public:
    LogBuffer();
    void   addLog(LogLevel level, const char* format, va_list args);
    void   addLog(LogLevel level, const String& message);
    String getLogsJSON() const;
    void   clear();
};

/** Global log buffer instance — written by LOG_* macros, read by /api/logs. */
extern LogBuffer g_log_buffer;

// ============================================================================
// Debug Mode — GPIO12 hardware trigger
// ============================================================================
// GPIO12 is pulled HIGH internally. Connecting it to GND enables verbose
// debug output at runtime without a firmware rebuild.
// ============================================================================
constexpr uint8_t DEBUG_MODE_PIN = 12;

/** Configure GPIO12 with internal pull-up. Call during system init. */
void initDebugModePin();

/** Returns true when GPIO12 is LOW (debug jumper installed). */
bool isDebugModeEnabled();

/** Initialise the async Serial logging queue and writer task. */
void initAsyncLogging();

/** Enqueue a message for Serial output without blocking the calling task. */
void logToSerialAsync(const char* msg);

// ============================================================================
// Internal sink macros — write to Serial (async) and the web log buffer
// ============================================================================
#define WEB_LOG_DEBUG(fmt, ...) do { \
    char rawBuf[512]; \
    snprintf(rawBuf, sizeof(rawBuf), fmt, ##__VA_ARGS__); \
    logToSerialAsync(("[DEBUG] " + String(rawBuf)).c_str()); \
    g_log_buffer.addLog(LOG_LEVEL_DEBUG, rawBuf); \
} while(0)

#define WEB_LOG_INFO(fmt, ...) do { \
    char rawBuf[512]; \
    snprintf(rawBuf, sizeof(rawBuf), fmt, ##__VA_ARGS__); \
    logToSerialAsync(("[INFO] " + String(rawBuf)).c_str()); \
    g_log_buffer.addLog(LOG_LEVEL_INFO, rawBuf); \
} while(0)

#define WEB_LOG_WARN(fmt, ...) do { \
    char rawBuf[512]; \
    snprintf(rawBuf, sizeof(rawBuf), fmt, ##__VA_ARGS__); \
    logToSerialAsync(("[WARN] " + String(rawBuf)).c_str()); \
    g_log_buffer.addLog(LOG_LEVEL_WARN, rawBuf); \
} while(0)

#define WEB_LOG_ERROR(fmt, ...) do { \
    char rawBuf[512]; \
    snprintf(rawBuf, sizeof(rawBuf), fmt, ##__VA_ARGS__); \
    logToSerialAsync(("[ERROR] " + String(rawBuf)).c_str()); \
    g_log_buffer.addLog(LOG_LEVEL_ERROR, rawBuf); \
} while(0)

// ============================================================================
// Public LOG_* macros — use these throughout the codebase
// ============================================================================
#ifndef MIN_LOG_LEVEL
#define MIN_LOG_LEVEL 0
#endif

// LOG_DEBUG is always guarded by the hardware pin at runtime
#define LOG_DEBUG(fmt, ...) do { \
    if (isDebugModeEnabled()) { \
        WEB_LOG_DEBUG(fmt, ##__VA_ARGS__); \
    } \
} while(0)

#if MIN_LOG_LEVEL <= 1
#define LOG_INFO WEB_LOG_INFO
#else
#define LOG_INFO(...)
#endif

#if MIN_LOG_LEVEL <= 2
#define LOG_WARN WEB_LOG_WARN
#else
#define LOG_WARN(...)
#endif

#if MIN_LOG_LEVEL <= 3
#define LOG_ERROR WEB_LOG_ERROR
#else
#define LOG_ERROR(...)
#endif