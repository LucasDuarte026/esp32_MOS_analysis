#pragma once

#include <Arduino.h>
#include <vector>

// Log levels
enum LogLevel {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_ERROR = 3
};

// Single log entry
struct LogEntry {
    unsigned long timestamp_ms;
    LogLevel level;
    String message;
};

// Circular buffer for logs
class LogBuffer {
private:
    static const size_t MAX_LOGS = 50;  // Keep last 50 log entries
    std::vector<LogEntry> logs_;
    size_t write_index_ = 0;
    bool buffer_full_ = false;
    SemaphoreHandle_t mutex_; // Mutex for thread safety

public:
    LogBuffer();
    void addLog(LogLevel level, const char* format, va_list args);
    void addLog(LogLevel level, const String& message);
    String getLogsJSON() const;
    void clear();
};

// Global log buffer instance
extern LogBuffer g_log_buffer;

// Fix #2: Increased buffer size to 512 for safety
// Macro wrappers that add to both Serial and web buffer
// Async logging control
void initAsyncLogging();
void logToSerialAsync(const char* msg);

// Macro wrappers that add to both Serial (Async) and web buffer
#define WEB_LOG_DEBUG(fmt, ...) do { \
    char buf[512]; \
    int len = snprintf(buf, sizeof(buf), "[DEBUG] " fmt, ##__VA_ARGS__); \
    if (len >= (int)sizeof(buf)) { \
         /* Truncated */ \
    } \
    logToSerialAsync(buf); \
    /* Strip prefix for Web Buffer to match original format if needed, but keeping full msg is fine */ \
    /* Or we can reconstruct raw message for web buffer? */ \
    /* Original used Serial.printf("[DEBUG] "...) then snprintf(buf, fmt...) for web. */ \
    /* For web buffer, we usually want JUST the message without [DEBUG] prefix? */ \
    /* Existing code: snprintf(buf, ... fmt) -> g_log_buffer.addLog(LOG_LEVEL_DEBUG, buf) */ \
    /* The LOG_LEVEL handles the type in JSON */ \
    /* So I should format TWICE? Or format ONCE with prefix for Serial, and ONCE without for Web? */ \
    /* Optimization: Format WITHOUT prefix. Add prefix for Serial? */ \
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

// Backward Compatibility / Short Aliases with Build Flag filtering
#ifndef MIN_LOG_LEVEL
#define MIN_LOG_LEVEL 0
#endif

#if MIN_LOG_LEVEL <= 0
#define LOG_DEBUG WEB_LOG_DEBUG
#else
#define LOG_DEBUG(...)
#endif

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
