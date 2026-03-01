#include "hardware_hal.h"
#include "log_buffer.h"
#include <driver/dac.h>
#include <Wire.h>
#include <memory>
#include <cmath>

namespace hal {

// ============================================================================
// InternalDAC Implementation
// ============================================================================

InternalDAC::InternalDAC(uint8_t channel, float maxVoltage)
    : channel_(channel), maxVoltage_(maxVoltage) {}

void InternalDAC::begin() {
    if (initialized_) {
        LOG_WARN("DAC channel %d already initialized", channel_);
        return;
    }
    dac_channel_t dacChannel = (channel_ == 1) ? DAC_CHANNEL_1 : DAC_CHANNEL_2;
    esp_err_t err = dac_output_enable(dacChannel);
    if (err == ESP_OK) {
        dac_output_voltage(dacChannel, 0);
        initialized_ = true;
        LOG_INFO("InternalDAC CH%d initialized (GPIO%d, 8-bit)",
                 channel_, (channel_ == 1) ? DAC_VDS_PIN : DAC_VGS_PIN);
    } else {
        LOG_ERROR("Failed to initialize InternalDAC ch%d: %d", channel_, err);
    }
}

void InternalDAC::setVoltage(float voltage) {
    if (!initialized_) { LOG_ERROR("InternalDAC ch%d not initialized!", channel_); return; }
    if (voltage < 0.0f) voltage = 0.0f;
    if (voltage > maxVoltage_) voltage = maxVoltage_;
    currentValue_ = static_cast<uint8_t>((voltage / DAC_VREF) * DAC_MAX_VALUE);
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
    if (oversamplingCount_ < 1)   oversamplingCount_ = 1;
    if (oversamplingCount_ > 256) oversamplingCount_ = 256;
}

void InternalADC::begin() {
    if (initialized_) { LOG_WARN("InternalADC on GPIO%d already initialized", pin_); return; }
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation(ADC_11db);
    pinMode(pin_, INPUT);
    initialized_ = true;
    LOG_INFO("InternalADC initialized on GPIO%d (%d-bit, %d samples, ~%.1f ENOB)",
             pin_, ADC_RESOLUTION, oversamplingCount_, getEffectiveBits());
}

uint16_t InternalADC::readRaw() {
    if (!initialized_) { LOG_ERROR("InternalADC GPIO%d not initialized!", pin_); return 0; }
    return analogRead(pin_);
}

float InternalADC::readVoltage() {
    if (!initialized_) { LOG_ERROR("InternalADC GPIO%d not initialized!", pin_); return 0.0f; }

    // ── Sample collection ──────────────────────────────────────────────────
    uint16_t samples[256];
    const uint16_t n = oversamplingCount_;
    for (uint16_t i = 0; i < n; i++) {
        samples[i] = static_cast<uint16_t>(analogRead(pin_));
    }

    // ── Insertion Sort ─────────────────────────────────────────────────────
    for (uint16_t i = 1; i < n; i++) {
        uint16_t key = samples[i];
        int16_t  j   = static_cast<int16_t>(i) - 1;
        while (j >= 0 && samples[j] > key) { samples[j + 1] = samples[j]; j--; }
        samples[j + 1] = key;
    }

    // ── Trimmed Mean (10% / 10%) ───────────────────────────────────────────
    const uint16_t trim  = n / 10;
    const uint16_t start = trim;
    const uint16_t end   = n - trim;
    uint32_t sum = 0; uint16_t count = 0;
    for (uint16_t i = start; i < end; i++) { sum += samples[i]; count++; }
    if (count == 0) count = 1;

    const float avgRaw = static_cast<float>(sum) / count;
    return (avgRaw / ADC_MAX_VALUE) * ADC_VREF;
}

void InternalADC::setOversamplingCount(uint16_t count) {
    if (count < 1)   count = 1;
    if (count > 256) count = 256;
    oversamplingCount_ = count;
    LOG_DEBUG("InternalADC oversampling → %d samples (~%.1f ENOB)", count, getEffectiveBits());
}

float InternalADC::getEffectiveBits() const {
    return ADC_RESOLUTION + (log2f(oversamplingCount_) / 2.0f);
}


// ============================================================================
// ExternalDAC (MCP4725) Implementation
// ============================================================================

ExternalDAC::ExternalDAC(uint8_t i2cAddr, float maxVoltage)
    : i2cAddr_(i2cAddr), maxVoltage_(maxVoltage) {}

bool ExternalDAC::begin() {
    if (initialized_) { LOG_WARN("ExternalDAC (0x%02X) already initialized", i2cAddr_); return true; }
    if (!mcp_.begin(i2cAddr_)) {
        LOG_ERROR("ExternalDAC MCP4725 not found at I2C addr 0x%02X", i2cAddr_);
        return false;
    }
    mcp_.setVoltage(0, false);  // Start at 0 V (no EEPROM write)
    initialized_ = true;
    LOG_INFO("ExternalDAC MCP4725 initialized at 0x%02X (12-bit, %.3f mV/step)",
             i2cAddr_, getResolution() * 1000.0f);
    return true;
}

void ExternalDAC::setVoltage(float voltage) {
    if (!initialized_) { LOG_ERROR("ExternalDAC 0x%02X not initialized!", i2cAddr_); return; }
    if (voltage < 0.0f)       voltage = 0.0f;
    if (voltage > maxVoltage_) voltage = maxVoltage_;

    // Convert voltage → 12-bit DAC code (0–4095)
    // MCP4725 output = (code / 4096) * VDD
    currentValue_ = static_cast<uint16_t>((voltage / EXT_DAC_VREF) * EXT_DAC_MAX_VALUE);
    if (currentValue_ > EXT_DAC_MAX_VALUE) currentValue_ = EXT_DAC_MAX_VALUE;

    mcp_.setVoltage(currentValue_, false);  // Write to DAC register, not EEPROM
}

void ExternalDAC::shutdown() {
    if (!initialized_) return;
    mcp_.setVoltage(0, false);
    currentValue_ = 0;
}


// ============================================================================
// ExternalADC (ADS1115) Implementation
// ============================================================================

ExternalADC::ExternalADC(uint8_t i2cAddr, uint16_t oversamplingCount)
    : i2cAddr_(i2cAddr), oversamplingCount_(oversamplingCount) {
    if (oversamplingCount_ < 1)   oversamplingCount_ = 1;
    if (oversamplingCount_ > 256) oversamplingCount_ = 256;
}

bool ExternalADC::begin() {
    if (initialized_) { LOG_WARN("ExternalADC (0x%02X) already initialized", i2cAddr_); return true; }
    if (!ads_.begin(i2cAddr_)) {
        LOG_ERROR("ExternalADC ADS1115 not found at I2C addr 0x%02X", i2cAddr_);
        return false;
    }
    // GAIN_TWO: ±2.048 V FSR → 0.0625 mV/LSB (best for 0–3.3 V range)
    ads_.setGain(GAIN_TWO);
    // 860 SPS: fastest rate → ~1.16 ms/sample (vs 7.8 ms at default 128 SPS)
    // With 64 oversampling samples: ~74 ms/point (vs ~500 ms at 128 SPS)
    ads_.setDataRate(RATE_ADS1115_860SPS);
    initialized_ = true;
    LOG_INFO("ExternalADC ADS1115 initialized at 0x%02X (16-bit, GAIN_TWO, %d samples, ~%.1f ENOB)",
             i2cAddr_, oversamplingCount_, getEffectiveBits());
    return true;
}

uint16_t ExternalADC::readRaw() {
    if (!initialized_) { LOG_ERROR("ExternalADC 0x%02X not initialized!", i2cAddr_); return 0; }
    int16_t raw = ads_.readADC_SingleEnded(0);  // Channel A0
    return (raw < 0) ? 0 : static_cast<uint16_t>(raw);
}

float ExternalADC::readVoltage() {
    if (!initialized_) { LOG_ERROR("ExternalADC 0x%02X not initialized!", i2cAddr_); return 0.0f; }

    // ── Sample collection (same algorithm as InternalADC) ──────────────────
    // ADS1115 raw: signed 16-bit; clamp negatives to 0 (0 V floor)
    uint16_t samples[256];
    const uint16_t n = oversamplingCount_;
    for (uint16_t i = 0; i < n; i++) {
        int16_t raw = ads_.readADC_SingleEnded(0);
        samples[i] = (raw < 0) ? 0 : static_cast<uint16_t>(raw);
    }

    // ── Insertion Sort ─────────────────────────────────────────────────────
    for (uint16_t i = 1; i < n; i++) {
        uint16_t key = samples[i];
        int16_t  j   = static_cast<int16_t>(i) - 1;
        while (j >= 0 && samples[j] > key) { samples[j + 1] = samples[j]; j--; }
        samples[j + 1] = key;
    }

    // ── Trimmed Mean (10% / 10%) ───────────────────────────────────────────
    const uint16_t trim  = n / 10;
    const uint16_t start = trim;
    const uint16_t end   = n - trim;
    uint32_t sum = 0; uint16_t count = 0;
    for (uint16_t i = start; i < end; i++) { sum += samples[i]; count++; }
    if (count == 0) count = 1;

    const float avgRaw = static_cast<float>(sum) / count;
    // ADS1115 voltage = raw * (FSR / 32767) = raw * (2.048 / 32767)
    return avgRaw * (EXT_ADC_VREF / static_cast<float>(EXT_ADC_MAX_RAW));
}

void ExternalADC::setOversamplingCount(uint16_t count) {
    if (count < 1)   count = 1;
    if (count > 256) count = 256;
    oversamplingCount_ = count;
    LOG_DEBUG("ExternalADC oversampling → %d samples (~%.1f ENOB)", count, getEffectiveBits());
}

float ExternalADC::getEffectiveBits() const {
    // ADS1115 is a true 16-bit sigma-delta ADC; oversampling still helps with noise.
    return EXT_ADC_BITS + (log2f(oversamplingCount_) / 2.0f);
}


// ============================================================================
// HardwareHAL Singleton Implementation
// ============================================================================

HardwareHAL& HardwareHAL::instance() {
    static HardwareHAL inst;
    return inst;
}

void HardwareHAL::begin(const HalConfig& config) {
    if (initialized_) {
        LOG_WARN("HardwareHAL already initialized — use switchMode() to change mode");
        return;
    }
    currentMode_ = config.hardware_mode;
    if (currentMode_ == HardwareMode::HW_INTERNAL) {
        initInternal(config);
    } else {
        initExternal(config);
    }
    initialized_ = true;
    LOG_INFO("HardwareHAL initialized in %s mode",
             currentMode_ == HardwareMode::HW_INTERNAL ? "HW_INTERNAL (ESP32)" : "HW_EXTERNAL (I2C)");
}

void HardwareHAL::switchMode(HardwareMode mode, const HalConfig& config) {
    // Always reinitialize — do NOT skip even if currentMode_ == mode.
    // Reason: hardware may have been absent at begin() time and silently fell
    // back to InternalDAC/InternalADC. An explicit switchMode() call (e.g.,
    // triggered by the user clicking "Start") must re-probe the I2C bus and
    // pick up any newly connected peripherals.

    // Shutdown current outputs for safety before switching
    if (dacVDS_)   dacVDS_->shutdown();
    if (dacVGS_)   dacVGS_->shutdown();

    // Release existing instances
    dacVDS_.reset();
    dacVGS_.reset();
    adcShunt_.reset();

    currentMode_ = mode;
    initialized_ = false;  // allow init logic below

    if (mode == HardwareMode::HW_INTERNAL) {
        initInternal(config);
    } else {
        initExternal(config);
    }

    initialized_ = true;
    LOG_INFO("HardwareHAL switched to %s mode",
             mode == HardwareMode::HW_INTERNAL ? "HW_INTERNAL (ESP32)" : "HW_EXTERNAL (I2C)");
}

void HardwareHAL::initInternal(const HalConfig& config) {
    // VDS DAC — InternalDAC channel 1 (GPIO25)
    auto vds = std::make_unique<InternalDAC>(1, config.max_vds);
    vds->begin();
    dacVDS_ = std::move(vds);

    // VGS DAC — InternalDAC channel 2 (GPIO26)
    auto vgs = std::make_unique<InternalDAC>(2, config.max_vgs);
    vgs->begin();
    dacVGS_ = std::move(vgs);

    // Shunt ADC — InternalADC (GPIO34)
    auto adc = std::make_unique<InternalADC>(config.adc_shunt_pin, config.adc_oversampling);
    adc->begin();
    adcShunt_ = std::move(adc);

    LOG_INFO("  [INTERNAL] VDS: InternalDAC GPIO%d (8-bit, %.1f mV/step)",
             DAC_VDS_PIN, dacVDS_->getResolution() * 1000.0f);
    LOG_INFO("  [INTERNAL] VGS: InternalDAC GPIO%d (8-bit, %.1f mV/step)",
             DAC_VGS_PIN, dacVGS_->getResolution() * 1000.0f);
    LOG_INFO("  [INTERNAL] ADC: InternalADC  GPIO%d (12-bit, %d samples)",
             config.adc_shunt_pin, config.adc_oversampling);
}

void HardwareHAL::initExternal(const HalConfig& config) {
    // VDS DAC — InternalDAC channel 1 (GPIO25) — stays internal in EXTERNAL mode
    auto vds = std::make_unique<InternalDAC>(1, config.max_vds);
    vds->begin();
    dacVDS_ = std::move(vds);

    // VGS DAC — ExternalDAC MCP4725 (I2C 0x60)
    auto vgs = std::make_unique<ExternalDAC>(EXT_DAC_VGS_ADDR, config.max_vgs);
    if (!vgs->begin()) {
        LOG_ERROR("ExternalDAC (MCP4725) init failed — falling back to InternalDAC for VGS");
        auto vgs_fallback = std::make_unique<InternalDAC>(2, config.max_vgs);
        vgs_fallback->begin();
        dacVGS_ = std::move(vgs_fallback);
    } else {
        dacVGS_ = std::move(vgs);
    }

    // Shunt ADC — ExternalADC ADS1115 (I2C 0x48, channel A0)
    auto adc = std::make_unique<ExternalADC>(EXT_ADC_ADDR, config.adc_oversampling);
    if (!adc->begin()) {
        LOG_ERROR("ExternalADC (ADS1115) init failed — falling back to InternalADC");
        auto adc_fallback = std::make_unique<InternalADC>(config.adc_shunt_pin, config.adc_oversampling);
        adc_fallback->begin();
        adcShunt_ = std::move(adc_fallback);
    } else {
        adcShunt_ = std::move(adc);
    }

    LOG_INFO("  [EXTERNAL] VDS: InternalDAC GPIO%d (8-bit)",  DAC_VDS_PIN);
    LOG_INFO("  [EXTERNAL] VGS: ExternalDAC MCP4725 0x%02X (12-bit, %.3f mV/step)",
             EXT_DAC_VGS_ADDR, dacVGS_->getResolution() * 1000.0f);
    LOG_INFO("  [EXTERNAL] ADC: ExternalADC ADS1115 0x%02X (16-bit, %d samples, ~%.1f ENOB)",
             EXT_ADC_ADDR, config.adc_oversampling, adcShunt_->getEffectiveBits());
}

void HardwareHAL::shutdown() {
    if (!initialized_) return;
    dacVDS_->shutdown();
    dacVGS_->shutdown();
    LOG_INFO("HardwareHAL shutdown: all outputs → 0 V");
}

// ============================================================================
// External Hardware Connectivity Check (I2C probe)
// ============================================================================

/**
 * Sends a 0-byte I2C transaction to the target address.
 * Returns true if the device ACKs (endTransmission == 0).
 * Non-destructive — only probes the bus, does not reinitialize anything.
 */
static bool probeI2CDevice(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

HardwareHAL::ExternalDeviceStatus HardwareHAL::checkExternalDevices() {
    // Ensure Wire is started (may already be running; calling begin() again is safe)
    Wire.begin();

    ExternalDeviceStatus status;
    status.mcp4725_vgs = probeI2CDevice(EXT_DAC_VGS_ADDR);  // 0x60
    status.ads1115     = probeI2CDevice(EXT_ADC_ADDR);       // 0x48
    return status;
}

// ============================================================================
// Legacy Compatibility Functions
// ============================================================================

void init()                   { HardwareHAL::instance().begin(); }
void setVDS(float voltage)    { HardwareHAL::instance().getVDS().setVoltage(voltage); }
void setVGS(float voltage)    { HardwareHAL::instance().getVGS().setVoltage(voltage); }
float readShuntVoltage()      { return HardwareHAL::instance().getShuntADC().readVoltage(); }
void shutdown()               { HardwareHAL::instance().shutdown(); }

} // namespace hal
