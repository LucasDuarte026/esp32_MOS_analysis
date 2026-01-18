#pragma once

#include <Arduino.h>

namespace monitoring
{
    // System status structure
    struct SystemStatus
    {
        float temperature_celsius;
        bool usb_connected;
        uint64_t chip_id;
        uint32_t free_heap;
        unsigned long last_update_ms;
    };

    // Initialize monitoring system
    void begin();

    // Get current system status (thread-safe)
    SystemStatus getStatus();

    // Background task function (runs on separate core)
    void monitoringTask(void *parameter);

} // namespace monitoring
