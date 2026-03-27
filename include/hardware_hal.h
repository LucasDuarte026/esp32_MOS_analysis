#ifndef HARDWARE_HAL_H
#define HARDWARE_HAL_H

#include <Arduino.h>
#include <memory>
#include <cmath>

// External I2C peripheral libraries
#include <Adafruit_MCP4725.h>
#include <Adafruit_ADS1X15.h>

// ============================================================================
// Hardware Abstraction Layer
// ============================================================================
// Interfaces, configuration, and concrete implementations for voltage sources
// (DAC) and current sensors (ADC) used in MOSFET characterization.
//
// Operating modes (selected at runtime via HardwareHAL::switchMode()):
//
//   HW_INTERNAL mode:
//     - DAC VDS : InternalDAC ch1 (GPIO25, 8-bit)
//     - DAC VGS : InternalDAC ch2 (GPIO26, 8-bit)
//     - ADC     : InternalADC     (GPIO34, 12-bit + oversampling)
//
//   HW_EXTERNAL mode (default) — fully I2C since v4.2.0:
//     - DAC VDS : ExternalDAC2 MCP4725 (I2C 0x61, ADDR→VCC, 12-bit)
//     - DAC VGS : ExternalDAC  MCP4725 (I2C 0x60, ADDR→GND, 12-bit)
//     - ADC     : ExternalADC  ADS1115 (I2C 0x48, A0, 16-bit + oversampling)
// ============================================================================

namespace hal {

// ============================================================================
// Hardware Mode
// ============================================================================
/**
 * @brief Selects which set of peripherals to use for DAC/ADC operations.
 * Note: named HW_INTERNAL / HW_EXTERNAL to avoid clash with Arduino's
 *       #define EXTERNAL 0 macro in Arduino.h.
 */
enum class HardwareMode {
    HW_INTERNAL,  ///< All ESP32 native DAC/ADC
    HW_EXTERNAL   ///< External I2C peripherals (MCP4725 VGS + ADS1115 ADC)
};

// ============================================================================
// Abstract Interfaces
// ============================================================================

/**
 * @brief Abstract interface for voltage source (DAC).
 *
 * Implementations provide voltage output with a defined range and resolution.
 * Used for controlling VDS and VGS in MOSFET characterization.
 */
class IVoltageSource {
public:
    virtual ~IVoltageSource() = default;

    /** Set output voltage (clamped to valid range). */
    virtual void    setVoltage(float voltage) = 0;
    /** Maximum output voltage in Volts. */
    virtual float   getMaxVoltage() const = 0;
    /** Voltage step size in Volts (FSR / 2^bits). */
    virtual float   getResolution() const = 0;
    /** DAC resolution in bits (e.g., 8 for ESP32, 12 for MCP4725). */
    virtual uint8_t getBits() const = 0;
    /** Set output to 0 V (safety shutdown). */
    virtual void    shutdown() = 0;
    /** Hardware name (e.g., "MCP4725 0x60"). */
    virtual const char* getName() const = 0;
};

/**
 * @brief Abstract interface for voltage/current sensor (ADC).
 *
 * Implementations provide noise-reduced voltage readings via oversampling
 * (Insertion Sort + Trimmed Mean). Used to read the shunt resistor voltage.
 */
class ICurrentSensor {
public:
    virtual ~ICurrentSensor() = default;

    /** Read voltage from the default/primary channel (oversampled). */
    virtual float    readVoltage() = 0;
    
    /** Read voltage from a specific channel (oversampled). */
    virtual float    readVoltage(uint8_t channel, uint8_t gainOverride = 255) = 0;
    
    /** Read voltage (fast 1-3 samples max, no heavy filtering). */
    virtual float    readVoltageFast() = 0;
    
    /** Read voltage from a specific channel (fast 1-3 samples max, no heavy filtering). */
    virtual float    readVoltageFast(uint8_t channel, uint8_t gainOverride = 255) = 0;
    
    /** Read single raw ADC value from default channel (no averaging). */
    virtual uint16_t readRaw() = 0;
    
    /** Read single raw ADC value from a specific channel (no averaging). */
    virtual uint16_t readRaw(uint8_t channel) = 0;
    /** Voltage step size in Volts (FSR / full-scale). */
    virtual float    getResolution() const = 0;
    /** Number of samples averaged per reading. */
    virtual uint16_t getOversamplingCount() const = 0;
    /** Set number of samples (1–256). */
    virtual void     setOversamplingCount(uint16_t count) = 0;
    /** Effective number of bits (ENOB) accounting for oversampling gain. */
    virtual float    getEffectiveBits() const = 0;
    
    /** Configure PGA gain if supported (blank default). */
    virtual void     setGain(uint8_t gainCode, bool silent = false) {}
    
    /** Retrieve the real gain dynamically picked by Auto-Ranging for a specific channel. */
    virtual uint8_t  getLastUsedGain(uint8_t channel) const { return 0; }
    
    /** Hardware name (e.g., "ADS1115 0x48"). */
    virtual const char* getName() const = 0;
};

// ============================================================================
// HAL Configuration
// ============================================================================
struct HalConfig {
    HardwareMode hardware_mode  = HardwareMode::HW_EXTERNAL;  // Default: external

    // Internal DAC pins (HW_INTERNAL mode only)
    uint8_t  dac_vds_pin      = 25;   // DAC Channel 1 — GPIO25 → VDS (Drain voltage)
    uint8_t  dac_vgs_pin      = 26;   // DAC Channel 2 — GPIO26 → VGS (Gate voltage)
    uint8_t  adc_shunt_pin    = 34;   // ADC1_CH6      — GPIO34 → shunt resistor read

    // Oversampling (applies to both internal and external ADC)
    uint16_t adc_oversampling = 16;

    // Reference voltages and safety limits
    float dac_vref       = 3.3f; // ESP32 Internal DAC Ref
    float adc_vref       = 3.3f; // ESP32 Internal ADC Ref
    float ext_dac_vref   = 5.0f; // MCP4725 VDD Ref
    float max_vds        = 5.0f; // Allow up to 5V (clipped internally later if needed)
    float max_vgs        = 5.0f; // Allow up to 5V (clipped internally later if needed)
};

// ============================================================================
// Pin / Hardware Constants
// ============================================================================
constexpr uint8_t  DAC_VDS_PIN    = 25;  // DAC Channel 1 — controls VDS (Drain)
constexpr uint8_t  DAC_VGS_PIN    = 26;  // DAC Channel 2 — controls VGS (Gate)
constexpr uint8_t  ADC_SHUNT_PIN  = 34;  // ADC1_CH6       — reads shunt resistor voltage

// Internal DAC (ESP32 — 8-bit, 0–3.3 V)
constexpr uint8_t  DAC_RESOLUTION  = 8;
constexpr uint16_t DAC_MAX_VALUE   = 255;
constexpr float    DAC_VREF        = 3.3f;

// Internal ADC (ESP32 — 12-bit, 0–3.3 V)
constexpr uint8_t  ADC_RESOLUTION      = 12;
constexpr uint16_t ADC_MAX_VALUE       = 4095;
constexpr float    ADC_VREF            = 3.3f;
constexpr uint16_t ADC_DEFAULT_SAMPLES = 64;

// External DAC (MCP4725 — 12-bit, 0–5.0 V typical)
constexpr uint8_t  EXT_DAC_VGS_ADDR  = 0x60;  // ADDR pin → GND
constexpr uint8_t  EXT_DAC_VDS_ADDR  = 0x61;  // ADDR pin → VCC
constexpr uint8_t  EXT_DAC_BITS      = 12;
constexpr uint16_t EXT_DAC_MAX_VALUE = 4095;
constexpr float    EXT_DAC_VREF      = 5.0f; // Updated to 5.0V based on hardware readings

// External ADC (ADS1115 — 16-bit, configurable PGA gain via setGain())
// Default FSR = GAIN_SIXTEEN (±0.256 V) for 1.33 Ω shunt / 20 mA max (V_shunt ~26.6 mV).
// The FSR constant below is the compile-time default; runtime gain is applied
// via ExternalADC::setGain(gainCode) before each sweep.
constexpr uint8_t  EXT_ADC_ADDR    = 0x48;    // ADDR pin → GND
constexpr uint8_t  EXT_ADC_BITS    = 16;
constexpr float    EXT_ADC_VREF    = 6.144f;  // Default FSR: GAIN_TWOTHIRDS (±6.144 V)
constexpr int16_t  EXT_ADC_MAX_RAW = 32767;   // Positive full-scale


// ============================================================================
// InternalDAC — ESP32 built-in 8-bit DAC
// ============================================================================
// Two independent channels:
//   channel 1 → GPIO25 → VDS (Drain voltage)   [both HW_INTERNAL and HW_EXTERNAL]
//   channel 2 → GPIO26 → VGS (Gate voltage)    [HW_INTERNAL only; replaced by MCP4725 in HW_EXTERNAL]
class InternalDAC : public IVoltageSource {
public:
    explicit InternalDAC(uint8_t channel, float maxVoltage = 3.3f);
    ~InternalDAC() override = default;

    void    setVoltage(float voltage) override;
    float   getMaxVoltage() const override { return maxVoltage_; }
    float   getResolution() const override { return DAC_VREF / (DAC_MAX_VALUE + 1); }
    uint8_t getBits() const override       { return DAC_RESOLUTION; }
    void    shutdown() override;
    const char* getName() const override { return "ESP32 DAC (8-bit)"; }

    void    begin();
    uint8_t getCurrentValue() const { return currentValue_; }

private:
    uint8_t channel_;
    float   maxVoltage_;
    uint8_t currentValue_ = 0;
    bool    initialized_  = false;
};


// ============================================================================
// InternalADC — ESP32 built-in 12-bit ADC with oversampling
// ============================================================================
/**
 * Oversampling strategy: Insertion Sort + Trimmed Mean (10% each side).
 *   1. Collect N raw readings.
 *   2. Sort with Insertion Sort (stack-only, MCU-friendly).
 *   3. Discard bottom 10% and top 10%.
 *   4. Average the central 80%.
 *
 * Effective ENOB gain ≈ log2(N)/2  (e.g., 64× → ~15 ENOB from 12-bit ADC).
 */
class InternalADC : public ICurrentSensor {
public:
    explicit InternalADC(uint8_t pin, uint16_t oversamplingCount = ADC_DEFAULT_SAMPLES);
    ~InternalADC() override = default;

    float    readVoltage() override;
    float    readVoltage(uint8_t channel, uint8_t gainOverride = 255) override;
    
    float    readVoltageFast() override;
    float    readVoltageFast(uint8_t channel, uint8_t gainOverride = 255) override;
    uint16_t readRaw() override;
    uint16_t readRaw(uint8_t channel) override;
    float    getResolution() const override         { return ADC_VREF / (ADC_MAX_VALUE + 1); }
    uint16_t getOversamplingCount() const override  { return oversamplingCount_; }
    void     setOversamplingCount(uint16_t count) override;
    float    getEffectiveBits() const override;
    const char* getName() const override { return "ESP32 ADC (12-bit)"; }

    void begin();

private:
    uint8_t  pin_;
    uint16_t oversamplingCount_;
    bool     initialized_ = false;
};


// ============================================================================
// ExternalDAC — MCP4725 I2C 12-bit DAC
// ============================================================================
// Controls VGS (Gate voltage) in HW_EXTERNAL mode.
// I2C address: 0x60 (ADDR pin tied to GND).
//
// No oversampling on write — MCP4725 is a true 12-bit DAC; a single I2C
// write is deterministic and does not benefit from averaging.
class ExternalDAC : public IVoltageSource {
public:
    explicit ExternalDAC(uint8_t i2cAddr, float maxVoltage = 5.0f);
    ~ExternalDAC() override = default;

    void    setVoltage(float voltage) override;
    float   getMaxVoltage() const override { return maxVoltage_; }
    float   getResolution() const override { return extDacVref_ / (EXT_DAC_MAX_VALUE + 1); }
    uint8_t getBits() const override       { return EXT_DAC_BITS; }
    void    shutdown() override;
    const char* getName() const override { return name_; }

    /** Initialize the MCP4725. Returns true on success. */
    bool begin();

    /** Set the actual MCP4725 supply voltage for accurate DAC code scaling.
     *  Valid range: 4.0 – 5.5 V. Values outside range are clamped and logged. */
    void setExtDacVref(float vref);

private:
    uint8_t          i2cAddr_;
    float            maxVoltage_;
    float            extDacVref_  = EXT_DAC_VREF; // runtime VDD, updated via setExtDacVref()
    uint16_t         currentValue_ = 0;
    bool             initialized_  = false;
    char             name_[20];
    Adafruit_MCP4725 mcp_;
};


// ============================================================================
// ExternalADC — ADS1115 I2C 16-bit ADC with oversampling
// ============================================================================
// Reads the shunt resistor voltage in HW_EXTERNAL mode.
// I2C address: 0x48 (ADDR pin tied to GND). Channel: A0.
// Gain: GAIN_TWO (±2.048 V FSR) for best resolution in 0–3.3 V range.
//
// Applies the same Insertion Sort + Trimmed Mean algorithm as InternalADC,
// using ads.readADC_SingleEnded(0) as the raw sample source.
class ExternalADC : public ICurrentSensor {
public:
    explicit ExternalADC(uint8_t i2cAddr = EXT_ADC_ADDR,
                         uint16_t oversamplingCount = ADC_DEFAULT_SAMPLES);
    ~ExternalADC() override = default;

    float    readVoltage() override;
    float    readVoltage(uint8_t channel, uint8_t gainOverride = 255) override; 
    
    float    readVoltageFast() override;
    float    readVoltageFast(uint8_t channel, uint8_t gainOverride = 255) override;
    uint16_t readRaw() override;
    uint16_t readRaw(uint8_t channel) override;
    float    getResolution() const override         { return EXT_ADC_VREF / (EXT_ADC_MAX_RAW + 1); }
    uint16_t getOversamplingCount() const override  { return oversamplingCount_; }
    void     setOversamplingCount(uint16_t count) override;
    float    getEffectiveBits() const override;
    uint8_t  getLastUsedGain(uint8_t channel) const override { if (channel < 4) return lastAutoGain_[channel]; return 0; }
    const char* getName() const override { return name_; }

    /** Initialize the ADS1115. Returns true on success. */
    bool begin();

    /**
     * @brief Apply a new PGA gain to the ADS1115 at runtime.
     * @param gainCode  Integer gain code sent from the web UI:
     *                  0 = ±6.144 V, 1 = ±4.096 V, 2 = ±2.048 V,
     *                  4 = ±1.024 V, 8 = ±0.512 V, 16 = ±0.256 V
     * Also updates the internal FSR used for voltage conversion.
     */
    void setGain(uint8_t gainCode, bool silent = false);

private:
    uint8_t          i2cAddr_;
    uint16_t         oversamplingCount_;
    bool             initialized_ = false;
    float            fsr_         = EXT_ADC_VREF;  // current FSR, updated by setGain()
    uint8_t          currentGainCode_ = 0;         // default to GAIN_TWOTHIRDS (0)
    uint8_t          lastAutoGain_[4] = {0, 0, 0, 0}; // memoized gain for Auto-Range mode
    char             name_[20];
    Adafruit_ADS1115 ads_;
};





// ============================================================================
// HardwareHAL — Singleton factory
// ============================================================================
/**
 * Manages VDS DAC, VGS DAC, and Shunt ADC through abstract interfaces.
 * Call begin() once at startup, then switchMode() to change peripherals
 * between measurements (not during a sweep).
 */
class HardwareHAL {
public:
    static HardwareHAL& instance();

    /** Initialize all hardware in the given mode. */
    void begin(const HalConfig& config = HalConfig());

    /**
     * @brief Switch peripheral mode at runtime.
     * @param mode    Target mode (HW_INTERNAL or HW_EXTERNAL)
     * @param config  Config to use for the new mode
     */
    void switchMode(HardwareMode mode, const HalConfig& config = HalConfig());

    /** Current operating mode. */
    HardwareMode getMode() const { return currentMode_; }

    /**
     * @brief Non-destructive I2C probe for external devices.
     *        Safe to call at any time; does NOT reinitialize anything.
     */
    struct ExternalDeviceStatus {
        bool mcp4725_vgs = false;  ///< DAC VGS  @ 0x60 (ADDR→GND)
        bool ads1115      = false;  ///< Shunt ADC @ 0x48
        bool all_ok() const { return mcp4725_vgs && ads1115; }
    };
    static ExternalDeviceStatus checkExternalDevices();

    IVoltageSource&  getVDS()      { return *dacVDS_; }
    IVoltageSource&  getVGS()      { return *dacVGS_; }
    ICurrentSensor&  getShuntADC() { return *adcShunt_; }
    ExternalDAC*     getExternalVGS();  // returns nullptr if not ExternalDAC
    
    /** Specialized reading methods assuming external ADC mapped to A0/A1/A2 */
    float readShuntVoltage(uint8_t gainCode = 255); 
    float readVD_Actual(uint8_t gainCode = 255);
    float readVG_Actual(uint8_t gainCode = 255);
    
    /** Fast specialized reading methods for iterative calibration */
    float readShuntVoltageFast(uint8_t gainCode = 255); 
    float readVD_ActualFast(uint8_t gainCode = 255);
    float readVG_ActualFast(uint8_t gainCode = 255);

    /** Configure ADS1115 PGA gain (0..16) */
    void setADC_Gain(uint8_t gainCode);
    
    /** Returns a string summary of the currently active hardware (e.g., "Fully External (VDS: MCP4725...)") */
    String getHardwareSummary() const;

    void shutdown();
    bool isInitialized() const { return initialized_; }

    HardwareHAL(const HardwareHAL&)            = delete;
    HardwareHAL& operator=(const HardwareHAL&) = delete;

private:
    HardwareHAL() = default;

    void initInternal(const HalConfig& config);
    void initExternal(const HalConfig& config);

    std::unique_ptr<IVoltageSource> dacVDS_;
    std::unique_ptr<IVoltageSource> dacVGS_;
    std::unique_ptr<ICurrentSensor> adcShunt_;

    HardwareMode currentMode_ = HardwareMode::HW_EXTERNAL;
    bool         initialized_ = false;
};

// ============================================================================
// Legacy compatibility functions (use HardwareHAL directly instead)
// ============================================================================
void  init();
void  setVDS(float voltage);
void  setVGS(float voltage);
float readShuntVoltage(uint8_t gainCode = 255);
float readVD_Actual(uint8_t gainCode = 255);
float readVG_Actual(uint8_t gainCode = 255);
float readShuntVoltageFast(uint8_t gainCode = 255);
float readVD_ActualFast(uint8_t gainCode = 255);
float readVG_ActualFast(uint8_t gainCode = 255);
void  setADC_Gain(uint8_t gainCode);
void  shutdown();

constexpr float getDACStepSize() { return DAC_VREF / (DAC_MAX_VALUE + 1); }
constexpr float getADCStepSize() { return ADC_VREF / (ADC_MAX_VALUE + 1); }

} // namespace hal

#endif // HARDWARE_HAL_H
