#ifndef HARDWARE_HAL_H
#define HARDWARE_HAL_H

#include <Arduino.h>
#include <memory>
#include "hal_interfaces.h"

// ============================================================================
// Hardware Abstraction Layer - Concrete Implementations
// ============================================================================
// ESP32 internal DAC and ADC implementations.
//
// Hardware Configuration:
//   - DAC1 (GPIO25): VDS control (0-3.3V, 8-bit resolution)
//   - DAC2 (GPIO26): VGS control (0-3.3V, 8-bit resolution)
//   - ADC  (GPIO34): Shunt voltage reading (0-3.3V, 12-bit + oversampling)
// ============================================================================

namespace hal {

// Pin Definitions (legacy compatibility)
constexpr uint8_t DAC_VDS_PIN = 25;    // DAC Channel 1
constexpr uint8_t DAC_VGS_PIN = 26;    // DAC Channel 2
constexpr uint8_t ADC_SHUNT_PIN = 34;  // ADC1_CH6

// DAC Configuration
constexpr uint8_t DAC_RESOLUTION = 8;            // 8-bit DAC
constexpr uint16_t DAC_MAX_VALUE = 255;          // 2^8 - 1
constexpr float DAC_VREF = 3.3f;                 // Reference voltage

// ADC Configuration
constexpr uint8_t ADC_RESOLUTION = 12;           // 12-bit ADC
constexpr uint16_t ADC_MAX_VALUE = 4095;         // 2^12 - 1
constexpr float ADC_VREF = 3.3f;                 // Reference voltage (with 11dB attenuation)
constexpr uint16_t ADC_DEFAULT_SAMPLES = 64;     // Default oversampling (was 16)

// Voltage limits (for safety)
constexpr float MAX_VDS_VOLTAGE = 3.3f;
constexpr float MAX_VGS_VOLTAGE = 3.3f;

// ============================================================================
// InternalDAC - ESP32 built-in 8-bit DAC
// ============================================================================

/**
 * @brief ESP32 internal DAC implementation
 * 
 * Uses dac_output_voltage() from ESP-IDF for reliable output.
 * Each instance controls one DAC channel (VDS or VGS).
 */
class InternalDAC : public IVoltageSource {
public:
    /**
     * @brief Construct DAC controller
     * @param channel DAC channel (1 for GPIO25, 2 for GPIO26)
     * @param maxVoltage Maximum output voltage (default 3.3V)
     */
    explicit InternalDAC(uint8_t channel, float maxVoltage = 3.3f);
    
    ~InternalDAC() override = default;
    
    void setVoltage(float voltage) override;
    float getMaxVoltage() const override { return maxVoltage_; }
    float getResolution() const override { return DAC_VREF / (DAC_MAX_VALUE + 1); }
    uint8_t getBits() const override { return DAC_RESOLUTION; }
    void shutdown() override;
    
    /**
     * @brief Initialize the DAC channel (must call before use)
     */
    void begin();
    
    /**
     * @brief Get current DAC value (0-255)
     */
    uint8_t getCurrentValue() const { return currentValue_; }

private:
    uint8_t channel_;
    float maxVoltage_;
    uint8_t currentValue_ = 0;
    bool initialized_ = false;
};

// ============================================================================
// InternalADC - ESP32 built-in 12-bit ADC with oversampling
// ============================================================================

/**
 * @brief ESP32 internal ADC with oversampling for noise reduction
 * 
 * Uses multiple samples averaged together to increase effective resolution.
 * With 64 samples, effective resolution increases by ~3 bits (12→15 ENOB).
 */
class InternalADC : public ICurrentSensor {
public:
    /**
     * @brief Construct ADC controller
     * @param pin GPIO pin for ADC (must be ADC1: GPIO32-39)
     * @param oversamplingCount Number of samples to average (default 64)
     */
    explicit InternalADC(uint8_t pin, uint16_t oversamplingCount = ADC_DEFAULT_SAMPLES);
    
    ~InternalADC() override = default;
    
    float readVoltage() override;
    uint16_t readRaw() override;
    float getResolution() const override { return ADC_VREF / (ADC_MAX_VALUE + 1); }
    uint16_t getOversamplingCount() const override { return oversamplingCount_; }
    void setOversamplingCount(uint16_t count) override;
    float getEffectiveBits() const override;
    
    /**
     * @brief Initialize the ADC (must call before use)
     */
    void begin();

private:
    uint8_t pin_;
    uint16_t oversamplingCount_;
    bool initialized_ = false;
};

// ============================================================================
// HardwareHAL - Factory class for managing hardware components
// ============================================================================

/**
 * @brief Singleton factory for hardware abstraction layer
 * 
 * Provides access to VDS DAC, VGS DAC, and Shunt ADC through abstract interfaces.
 * Allows easy replacement with external DAC/ADC in the future.
 */
class HardwareHAL {
public:
    /**
     * @brief Get singleton instance
     */
    static HardwareHAL& instance();
    
    /**
     * @brief Initialize all hardware (DACs and ADC)
     * @param config Optional configuration (uses defaults if not provided)
     */
    void begin(const HalConfig& config = HalConfig());
    
    /**
     * @brief Get VDS voltage source (DAC)
     */
    IVoltageSource& getVDS() { return *dacVDS_; }
    
    /**
     * @brief Get VGS voltage source (DAC)
     */
    IVoltageSource& getVGS() { return *dacVGS_; }
    
    /**
     * @brief Get shunt voltage sensor (ADC)
     */
    ICurrentSensor& getShuntADC() { return *adcShunt_; }
    
    /**
     * @brief Shutdown all outputs (safety)
     */
    void shutdown();
    
    /**
     * @brief Check if HAL is initialized
     */
    bool isInitialized() const { return initialized_; }
    
    // Delete copy constructor and assignment
    HardwareHAL(const HardwareHAL&) = delete;
    HardwareHAL& operator=(const HardwareHAL&) = delete;

private:
    HardwareHAL() = default;
    
    std::unique_ptr<InternalDAC> dacVDS_;
    std::unique_ptr<InternalDAC> dacVGS_;
    std::unique_ptr<InternalADC> adcShunt_;
    bool initialized_ = false;
};

// ============================================================================
// Legacy compatibility functions (deprecated but maintained for transition)
// ============================================================================

/**
 * @brief Initialize the HAL (legacy function)
 * @deprecated Use HardwareHAL::instance().begin() instead
 */
void init();

/**
 * @brief Set VDS voltage (legacy function)
 * @deprecated Use HardwareHAL::instance().getVDS().setVoltage() instead
 */
void setVDS(float voltage);

/**
 * @brief Set VGS voltage (legacy function)
 * @deprecated Use HardwareHAL::instance().getVGS().setVoltage() instead
 */
void setVGS(float voltage);

/**
 * @brief Read shunt voltage (legacy function)
 * @deprecated Use HardwareHAL::instance().getShuntADC().readVoltage() instead
 */
float readShuntVoltage();

/**
 * @brief Shutdown all outputs (legacy function)
 * @deprecated Use HardwareHAL::instance().shutdown() instead
 */
void shutdown();

// Helper functions (kept for backward compatibility)
constexpr float getDACStepSize() {
    return DAC_VREF / (DAC_MAX_VALUE + 1);
}

constexpr float getADCStepSize() {
    return ADC_VREF / (ADC_MAX_VALUE + 1);
}

} // namespace hal

#endif // HARDWARE_HAL_H
