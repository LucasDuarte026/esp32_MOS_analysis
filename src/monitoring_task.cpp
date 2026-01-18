#include "monitoring_task.h"

// FreeRTOS headers
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/semphr.h"
}

extern "C" float temperatureRead();

namespace monitoring
{
    namespace
    {
        // Shared data protected by mutex
        SystemStatus g_status = {0};
        SemaphoreHandle_t g_mutex = nullptr;

        // Update interval
        constexpr uint32_t UPDATE_INTERVAL_MS = 500;

        // ============================================================================
        // FIXME: USB Serial Detection - REQUIRES FUTURE MAINTENANCE
        // ============================================================================
        // Current Status: NOT WORKING CORRECTLY - Shows "active" erroneously
        // 
        // Problem: ESP32 uses UART-based Serial (not native USB like ESP32-S2/S3)
        //          Current detection method cannot reliably distinguish between:
        //          1. USB cable with power only (no PC/serial monitor)
        //          2. USB cable with active serial monitor connection
        //
        // Current Behavior: 
        //   - Often shows "Comunicação USB Ativa" even when no serial monitor is open
        //   - May show "Inativa" even when serial monitor IS connected
        //
        // TODO Future Solutions to Investigate:
        //   1. Hardware approach: Add DTR/RTS line detection via GPIO
        //   2. Software approach: Implement heartbeat protocol (PC must send periodic signal)
        //   3. Alternative: Use ESP32-S2/S3 with native USB support
        //   4. Simple fix: Remove USB detection entirely, assume WiFi-only operation
        //
        // For now: This feature is marked as "beta" / unreliable
        // ============================================================================
        
        static unsigned long last_check_time = 0;
        static bool last_usb_state = false;
        static int consecutive_checks = 0;
        static unsigned long last_print_time = 0;
        
        bool detectUSB()
        {
            // Check every 1 second
            unsigned long now = millis();
            if (now - last_check_time < 1000) {
                return last_usb_state;
            }
            last_check_time = now;
            
            // For ESP32 UART-based serial:
            // Check if buffer is valid and draining
            if (!Serial) return false;

            int writeable_before = Serial.availableForWrite();
            
            // Verify if we can write at all
            if (writeable_before < 0) return false;

            // Send a test byte (0x00) to check connection
            // If connected, this should be consumed quickly by the UART hardware
            Serial.write(0x00); 
            
            // Short delay to allow UART FIFO to process
            vTaskDelay(pdMS_TO_TICKS(10)); 
            
            int writeable_after = Serial.availableForWrite();
            
            // Logic:
            // If connected: Buffer should recover (space available increases or stays high)
            // If disconnected/stalled: Buffer fills up and space available decreases
            
            // We assume connected if we have plenty of space
            bool buffer_ok = (writeable_after > 100); 
            
            // Use hysteresis
            if (buffer_ok == last_usb_state) {
                consecutive_checks = 0;
            } else {
                consecutive_checks++;
                if (consecutive_checks >= 3) {
                    last_usb_state = buffer_ok;
                    consecutive_checks = 0;
                }
            }
            
            return last_usb_state;
        }

    } // namespace

    void begin()
    {
        g_mutex = xSemaphoreCreateMutex();
        if (g_mutex == nullptr)
        {
            Serial.println("Failed to create monitoring mutex!");
            return;
        }

        // Get chip ID (ESP32 MAC address)
        g_status.chip_id = ESP.getEfuseMac();

        // Create monitoring task on core 0 (loop runs on core 1)
        xTaskCreatePinnedToCore(
            monitoringTask,
            "MonitorTask",
            4096,
            nullptr,
            1, // Priority
            nullptr,
            0 // Core 0
        );

        Serial.println("Monitoring task started");
    }

    SystemStatus getStatus()
    {
        SystemStatus status_copy;

        if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            status_copy = g_status;
            xSemaphoreGive(g_mutex);
        }

        return status_copy;
    }

    void monitoringTask(void *parameter)
    {
        TickType_t last_wake = xTaskGetTickCount();

        while (true)
        {
            // Read temperature using Arduino ESP32 native function
            float temp = temperatureRead(); // Returns temperature in Celsius

            // Detect USB connection
            bool usb = detectUSB();

            // Get free heap
            uint32_t heap = ESP.getFreeHeap();

            // Update shared data
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                g_status.temperature_celsius = temp;
                g_status.usb_connected = usb;
                g_status.free_heap = heap;
                g_status.last_update_ms = millis();
                xSemaphoreGive(g_mutex);
            }

            // Wait for next update
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UPDATE_INTERVAL_MS));
        }
    }

} // namespace monitoring
