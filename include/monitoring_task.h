#pragma once

#include <Arduino.h>

namespace monitoring
{
    // System status structure (v2.0.0)
    struct SystemStatus
    {
        float temperature_celsius;
        bool usb_connected;         // Note: Always false on ESP32 UART (unreliable detection)
        uint64_t chip_id;
        uint32_t free_heap;
        unsigned long last_update_ms;
        
        // Storage info (v2.0.0)
        size_t storage_total = 0;
        size_t storage_used = 0;
        float storage_percent = 0.0f;
    };

    // Initialize monitoring system
    void begin();

    // Get current system status (thread-safe)
    SystemStatus getStatus();

    // Background task function (runs on separate core)
    void monitoringTask(void *parameter);

} // namespace monitoring
