#pragma once

#include <Arduino.h>

namespace monitoring
{

// ============================================================================
// SystemStatus — snapshot of ESP32 system health
// ============================================================================
/**
 * Populated periodically by the monitoring background task.
 * Read via getStatus() from any task; the implementation uses a mutex
 * to ensure a consistent snapshot.
 */
struct SystemStatus
{
    float         temperature_celsius; ///< Internal core temperature (°C)
    bool          usb_connected;       ///< USB VBUS detection — always false on UART-only builds
    uint64_t      chip_id;             ///< Unique 64-bit chip identifier (EFUSE MAC)
    uint32_t      free_heap;           ///< Free heap bytes at last update
    unsigned long last_update_ms;      ///< millis() when this snapshot was taken

    // Storage (populated since v2.0.0)
    size_t storage_total   = 0;    ///< FFat partition size in bytes
    size_t storage_used    = 0;    ///< Bytes currently used
    float  storage_percent = 0.0f; ///< storage_used / storage_total, range [0, 1]
};

/** Initialise the monitoring system and spawn its background task on Core 0. */
void begin();

/** Return a thread-safe snapshot of the current system status. */
SystemStatus getStatus();

/** Background task function — do not call directly; launched by begin(). */
void monitoringTask(void* parameter);

} // namespace monitoring

