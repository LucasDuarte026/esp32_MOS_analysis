#include "hardware_hal.h"
#include "log_buffer.h"
#include <driver/dac.h>
#include <memory>
#include <cmath>

namespace hal {

// ============================================================================
// InternalDAC Implementation
// ============================================================================

InternalDAC::InternalDAC(uint8_t channel, float maxVoltage)
    : channel_(channel), maxVoltage_(maxVoltage) {
}

void InternalDAC::begin() {
    if (initialized_) {
        LOG_WARN("DAC channel %d already initialized", channel_);
        return;
    }
    
    dac_channel_t dacChannel = (channel_ == 1) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    esp_err_t err = dac_output_enable(dacChannel);
    
    if (err == ESP_OK) {
        dac_output_voltage(dacChannel, 0);  // Start at 0V
        initialized_ = true;
        LOG_INFO("InternalDAC CH%d initialized (GPIO%d)", 
                 channel_, (channel_ == 1) ? DAC_VDS_PIN : DAC_VGS_PIN);
    } else {
        LOG_ERROR("Failed to initialize DAC channel %d: %d", channel_, err);
    }
}

void InternalDAC::setVoltage(float voltage) {
    if (!initialized_) {
        LOG_ERROR("DAC channel %d not initialized!", channel_);
        return;
    }
    
    // Clamp voltage to valid range
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > maxVoltage_) voltage = maxVoltage_;
    
    // Convert voltage to DAC value (0-255)
    currentValue_ = static_cast<uint8_t>((voltage / DAC_VREF) * DAC_MAX_VALUE);
    
    // Apply voltage using ESP-IDF DAC driver
    dac_channel_t dacChannel = (channel_ == 1) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    dac_output_voltage(dacChannel, currentValue_);
}

void InternalDAC::shutdown() {
    if (!initialized_) return;
    
    dac_channel_t dacChannel = (channel_ == 1) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    dac_output_voltage(dacChannel, 0);
    currentValue_ = 0;
}

// ============================================================================
// InternalADC Implementation
// ============================================================================

InternalADC::InternalADC(uint8_t pin, uint16_t oversamplingCount)
    : pin_(pin), oversamplingCount_(oversamplingCount) {
    // Clamp oversampling to reasonable range
    if (oversamplingCount_ < 1) oversamplingCount_ = 1;
    if (oversamplingCount_ > 256) oversamplingCount_ = 256;
}

void InternalADC::begin() {
    if (initialized_) {
        LOG_WARN("ADC on GPIO%d already initialized", pin_);
        return;
    }
    
    // Configure ADC
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation(ADC_11db);  // Full range: 0-3.3V
    pinMode(pin_, INPUT);
    
    initialized_ = true;
    LOG_INFO("InternalADC initialized on GPIO%d (%d-bit, %d samples, ~%.1f ENOB)",
             pin_, ADC_RESOLUTION, oversamplingCount_, getEffectiveBits());
}

uint16_t InternalADC::readRaw() {
    if (!initialized_) {
        LOG_ERROR("ADC on GPIO%d not initialized!", pin_);
        return 0;
    }
    return analogRead(pin_);
}

float InternalADC::readVoltage() {
    if (!initialized_) {
        LOG_ERROR("ADC on GPIO%d not initialized!", pin_);
        return 0.0f;
    }
    
    // Oversampling - sum multiple readings
    uint32_t sum = 0;
    for (uint16_t i = 0; i < oversamplingCount_; i++) {
        sum += analogRead(pin_);
    }
    
    // Calculate average
    float avgRaw = static_cast<float>(sum) / oversamplingCount_;
    
    // Convert to voltage
    return (avgRaw / ADC_MAX_VALUE) * ADC_VREF;
}

void InternalADC::setOversamplingCount(uint16_t count) {
    if (count < 1) count = 1;
    if (count > 256) count = 256;
    oversamplingCount_ = count;
    LOG_DEBUG("ADC oversampling set to %d samples (~%.1f ENOB)", 
              count, getEffectiveBits());
}

float InternalADC::getEffectiveBits() const {
    // Oversampling improves resolution by log2(N)/2 bits
    // e.g., 64 samples → 12 + log2(64)/2 = 12 + 3 = 15 effective bits
    return ADC_RESOLUTION + (log2(oversamplingCount_) / 2.0f);
}

// ============================================================================
// HardwareHAL Singleton Implementation
// ============================================================================

HardwareHAL& HardwareHAL::instance() {
    static HardwareHAL instance;
    return instance;
}

void HardwareHAL::begin(const HalConfig& config) {
    if (initialized_) {
        LOG_WARN("HardwareHAL already initialized");
        return;
    }
    
    LOG_INFO("Initializing Hardware HAL v2.0...");
    
    // Create DAC instances
    dacVDS_ = std::make_unique<InternalDAC>(1, config.max_vds);
    dacVGS_ = std::make_unique<InternalDAC>(2, config.max_vgs);
    
    // Create ADC instance
    adcShunt_ = std::make_unique<InternalADC>(config.adc_shunt_pin, config.adc_oversampling);
    
    // Initialize all components
    dacVDS_->begin();
    dacVGS_->begin();
    adcShunt_->begin();
    
    initialized_ = true;
    
    LOG_INFO("HardwareHAL initialized successfully");
    LOG_INFO("  VDS DAC: GPIO%d (%.1fmV/step)", 
             config.dac_vds_pin, dacVDS_->getResolution() * 1000.0f);
    LOG_INFO("  VGS DAC: GPIO%d (%.1fmV/step)", 
             config.dac_vgs_pin, dacVGS_->getResolution() * 1000.0f);
    LOG_INFO("  Shunt ADC: GPIO%d (%.2fmV/step, %d samples, %.1f ENOB)", 
             config.adc_shunt_pin, 
             adcShunt_->getResolution() * 1000.0f,
             adcShunt_->getOversamplingCount(),
             adcShunt_->getEffectiveBits());
}

void HardwareHAL::shutdown() {
    if (!initialized_) return;
    
    dacVDS_->shutdown();
    dacVGS_->shutdown();
    
    LOG_INFO("HardwareHAL shutdown: All outputs set to 0V");
}

// ============================================================================
// Legacy Compatibility Functions
// ============================================================================

void init() {
    HardwareHAL::instance().begin();
}

void setVDS(float voltage) {
    HardwareHAL::instance().getVDS().setVoltage(voltage);
}

void setVGS(float voltage) {
    HardwareHAL::instance().getVGS().setVoltage(voltage);
}

float readShuntVoltage() {
    return HardwareHAL::instance().getShuntADC().readVoltage();
}

void shutdown() {
    HardwareHAL::instance().shutdown();
}

} // namespace hal
