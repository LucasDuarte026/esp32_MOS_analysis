#include "hardware_hal.h"
#include "log_buffer.h"
#include <driver/dac.h>

namespace hal {

// Internal state
static bool initialized = false;

void init() {
    if (initialized) {
        LOG_WARN("HAL already initialized");
        return;
    }
    
    // Configure ADC
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation(ADC_11db);  // Full range: 0-3.3V
    pinMode(ADC_SHUNT_PIN, INPUT);
    
    // Enable DAC channels
    dac_output_enable(DAC_CHANNEL_1);  // GPIO25 for VDS
    dac_output_enable(DAC_CHANNEL_2);  // GPIO26 for VGS
    
    // Initialize outputs to 0V
    dac_output_voltage(DAC_CHANNEL_1, 0);
    dac_output_voltage(DAC_CHANNEL_2, 0);
    
    initialized = true;
    
    LOG_INFO("Hardware HAL initialized");
    LOG_INFO("  DAC VDS: GPIO%d (%.1fmV/step)", DAC_VDS_PIN, getDACStepSize() * 1000.0f);
    LOG_INFO("  DAC VGS: GPIO%d (%.1fmV/step)", DAC_VGS_PIN, getDACStepSize() * 1000.0f);
    LOG_INFO("  ADC Shunt: GPIO%d (%.2fmV/step, %d samples)", 
             ADC_SHUNT_PIN, getADCStepSize() * 1000.0f, ADC_SAMPLES);
}

void setVDS(float voltage) {
    // Clamp voltage to valid range
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > MAX_VDS_VOLTAGE) voltage = MAX_VDS_VOLTAGE;
    
    // Convert voltage to DAC value (0-255)
    uint8_t dacValue = (uint8_t)((voltage / DAC_VREF) * DAC_MAX_VALUE);
    
    // Apply voltage using ESP-IDF DAC driver
    dac_output_voltage(DAC_CHANNEL_1, dacValue);
}

void setVGS(float voltage) {
    // Clamp voltage to valid range
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > MAX_VGS_VOLTAGE) voltage = MAX_VGS_VOLTAGE;
    
    // Convert voltage to DAC value (0-255)
    uint8_t dacValue = (uint8_t)((voltage / DAC_VREF) * DAC_MAX_VALUE);
    
    // Apply voltage using ESP-IDF DAC driver
    dac_output_voltage(DAC_CHANNEL_2, dacValue);
}

float readShuntVoltage() {
    // Multiple samples for noise reduction
    uint32_t sum = 0;
    
    for (uint8_t i = 0; i < ADC_SAMPLES; i++) {
        sum += analogRead(ADC_SHUNT_PIN);
    }
    
    // Calculate average
    float avgRaw = (float)sum / ADC_SAMPLES;
    
    // Convert to voltage
    return (avgRaw / ADC_MAX_VALUE) * ADC_VREF;
}

void shutdown() {
    // Set both DACs to 0V for safety
    dac_output_voltage(DAC_CHANNEL_1, 0);
    dac_output_voltage(DAC_CHANNEL_2, 0);
    
    LOG_INFO("HAL shutdown: All outputs set to 0V");
}

} // namespace hal
