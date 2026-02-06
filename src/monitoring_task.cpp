#include "monitoring_task.h"
#include "debug_mode.h"
#include "led_status.h"
#include "file_manager.h"

// FreeRTOS headers
extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

        // USB Serial Detection
        // Note: This is still imperfect on ESP32 UART-based serial, but we can
        // attempt detection through Serial activity
        volatile bool g_usb_activity_detected = false;
        volatile unsigned long g_last_serial_activity = 0;
        
        bool detectUSBSerial() {
            // Method 1: Check if Serial is available and configured
            if (!Serial) return false;
            
            // Method 2: Check for recent Serial activity (RX or TX)
            // When Serial Monitor is open, there's typically some polling
            // We can also check if Serial is available for writing
            
            // Check if Serial has been used recently
            // This is a heuristic: if Serial.available() returns non-zero
            // or if we've seen TX activity, assume USB is connected
            unsigned long now = millis();
            
            // Consider USB active if we've had activity in last 5 seconds
            // or if Serial appears functional
            bool hasRecentActivity = (now - g_last_serial_activity) < 5000;
            
            // Simple check: try to see if baudrate is configured (non-zero)
            // and the port appears to be open
            bool serialConfigured = Serial.baudRate() > 0;
            
            return serialConfigured && (hasRecentActivity || Serial.availableForWrite() > 0);
        }
        
        // Call this from Serial logging to track activity
        void notifySerialActivity() {
            g_last_serial_activity = millis();
            g_usb_activity_detected = true;
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
        
        // Mark initial serial activity
        g_last_serial_activity = millis();
        
        // Initialize debug mode (GPIO12)
        debug_mode::init();

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

        // Log initial diagnostic values
        float initTemp = temperatureRead();
        uint32_t initHeap = ESP.getFreeHeap();
        Serial.printf("[MONITOR] Initial temp: %.1f°C, Free heap: %lu bytes\n", 
                      initTemp, (unsigned long)initHeap);
        
        // Log storage info
        StorageInfo storage = FileManager::getStorageInfo();
        Serial.printf("[MONITOR] Storage: %.1f%% used (%u/%u bytes)\n",
                      storage.percentUsed * 100.0f,
                      (unsigned)storage.usedBytes,
                      (unsigned)storage.totalBytes);
        
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

            // Get free heap
            uint32_t heap = ESP.getFreeHeap();
            
            // Get storage info (v2.0.0)
            StorageInfo storage = FileManager::getStorageInfo();
            
            // Detect USB Serial connection
            bool usb = detectUSBSerial();

            // Update debug mode state
            debug_mode::update();

            // Update shared data
            if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
            {
                g_status.temperature_celsius = temp;
                g_status.usb_connected = usb;
                g_status.free_heap = heap;
                g_status.last_update_ms = millis();
                
                // Storage info (v2.0.0)
                g_status.storage_total = storage.totalBytes;
                g_status.storage_used = storage.usedBytes;
                g_status.storage_percent = storage.percentUsed;
                
                xSemaphoreGive(g_mutex);
            }

            // Wait for next update
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(UPDATE_INTERVAL_MS));
        }
    }

} // namespace monitoring
