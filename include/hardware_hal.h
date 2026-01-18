#ifndef HARDWARE_HAL_H
#define HARDWARE_HAL_H

#include <Arduino.h>

// ============================================================================
// Hardware Abstraction Layer for MOSFET Characterization
// ============================================================================
// This module provides an abstraction over the ESP32's DAC and ADC peripherals
// for controlling VDS, VGS, and reading the shunt voltage.
//
// Hardware Configuration:
//   - DAC1 (GPIO25): VDS control (0-3.3V, 8-bit resolution)
//   - DAC2 (GPIO26): VGS control (0-3.3V, 8-bit resolution)
//   - ADC  (GPIO34): Shunt voltage reading (0-3.3V, 12-bit resolution)
// ============================================================================

namespace hal {

// Pin Definitions
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
constexpr uint8_t ADC_SAMPLES = 16;              // Number of samples for averaging

// Voltage limits (for safety)
constexpr float MAX_VDS_VOLTAGE = 3.3f;
constexpr float MAX_VGS_VOLTAGE = 3.3f;

/**
 * @brief Initialize the HAL (DACs and ADC)
 * Must be called once at startup before using other functions.
 */
void init();

/**
 * @brief Set the VDS (Drain-Source) voltage
 * @param voltage Target voltage (0.0 to 3.3V)
 * @note Values are clamped to valid range
 */
void setVDS(float voltage);

/**
 * @brief Set the VGS (Gate-Source) voltage
 * @param voltage Target voltage (0.0 to 3.3V)
 * @note Values are clamped to valid range
 */
void setVGS(float voltage);

/**
 * @brief Read the voltage on the shunt resistor
 * @return Measured voltage in Volts (0.0 to 3.3V)
 * @note Returns averaged value from multiple samples to reduce noise
 */
float readShuntVoltage();

/**
 * @brief Shutdown all outputs (set DACs to 0V)
 * Should be called when stopping measurements for safety
 */
void shutdown();

/**
 * @brief Get the actual DAC step size in volts
 * @return Voltage resolution per DAC step (~12.9mV for 8-bit)
 */
constexpr float getDACStepSize() {
    return DAC_VREF / (DAC_MAX_VALUE + 1);
}

/**
 * @brief Get the actual ADC step size in volts
 * @return Voltage resolution per ADC step (~0.8mV for 12-bit)
 */
constexpr float getADCStepSize() {
    return ADC_VREF / (ADC_MAX_VALUE + 1);
}

} // namespace hal

#endif // HARDWARE_HAL_H
