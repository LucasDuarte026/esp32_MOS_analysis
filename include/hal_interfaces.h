#ifndef HAL_INTERFACES_H
#define HAL_INTERFACES_H

#include <Arduino.h>

// ============================================================================
// Hardware Abstraction Layer - Interfaces
// ============================================================================
// Abstract interfaces for voltage sources (DAC) and current sensors (ADC).
// These allow swapping hardware implementations without changing business logic.
//
// Current Implementations:
//   - InternalDAC: Uses ESP32 built-in 8-bit DACs (GPIO25, GPIO26)
//   - InternalADC: Uses ESP32 built-in 12-bit ADC with oversampling
//
// Future Implementations (planned):
//   - MCP4725: External 12-bit I2C DAC
//   - ADS1115: External 16-bit I2C ADC
// ============================================================================

namespace hal {

/**
 * @brief Abstract interface for voltage source (DAC)
 * 
 * Implementations must provide voltage output capability with defined range
 * and resolution. Used for controlling VDS and VGS in MOSFET characterization.
 */
class IVoltageSource {
public:
    virtual ~IVoltageSource() = default;
    
    /**
     * @brief Set output voltage
     * @param voltage Target voltage in Volts (clamped to valid range)
     */
    virtual void setVoltage(float voltage) = 0;
    
    /**
     * @brief Get maximum output voltage
     * @return Maximum voltage in Volts
     */
    virtual float getMaxVoltage() const = 0;
    
    /**
     * @brief Get voltage resolution (step size)
     * @return Voltage step in Volts
     */
    virtual float getResolution() const = 0;
    
    /**
     * @brief Get number of bits for the DAC
     * @return Resolution in bits (e.g., 8 for ESP32 DAC)
     */
    virtual uint8_t getBits() const = 0;
    
    /**
     * @brief Shutdown output (set to 0V for safety)
     */
    virtual void shutdown() = 0;
};

/**
 * @brief Abstract interface for current/voltage sensor (ADC)
 * 
 * Implementations must provide voltage reading capability with noise reduction.
 * The readVoltage() method should include oversampling for improved ENOB.
 */
class ICurrentSensor {
public:
    virtual ~ICurrentSensor() = default;
    
    /**
     * @brief Read voltage from ADC with oversampling
     * @return Averaged voltage in Volts
     */
    virtual float readVoltage() = 0;
    
    /**
     * @brief Read raw ADC value (no averaging)
     * @return Raw ADC value (0-4095 for 12-bit)
     */
    virtual uint16_t readRaw() = 0;
    
    /**
     * @brief Get voltage resolution (step size)
     * @return Voltage step in Volts
     */
    virtual float getResolution() const = 0;
    
    /**
     * @brief Get number of oversampling samples
     * @return Number of samples averaged per reading
     */
    virtual uint16_t getOversamplingCount() const = 0;
    
    /**
     * @brief Set number of oversampling samples
     * @param count Number of samples to average (1-256)
     */
    virtual void setOversamplingCount(uint16_t count) = 0;
    
    /**
     * @brief Get effective resolution (ENOB) with oversampling
     * @return Effective number of bits
     */
    virtual float getEffectiveBits() const = 0;
};

/**
 * @brief Configuration for HAL initialization
 */
struct HalConfig {
    // DAC pins
    uint8_t dac_vds_pin = 25;    // DAC Channel 1
    uint8_t dac_vgs_pin = 26;    // DAC Channel 2
    
    // ADC pin
    uint8_t adc_shunt_pin = 34;  // ADC1_CH6
    
    // ADC oversampling (default: 64 samples for ~15-bit effective resolution)
    uint16_t adc_oversampling = 64;
    
    // Reference voltages
    float dac_vref = 3.3f;
    float adc_vref = 3.3f;
    
    // Voltage limits
    float max_vds = 3.3f;
    float max_vgs = 3.3f;
};

} // namespace hal

#endif // HAL_INTERFACES_H
