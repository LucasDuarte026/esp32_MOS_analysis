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
    return readRaw(pin_);
}

uint16_t InternalADC::readRaw(uint8_t channel) {
    if (!initialized_) { LOG_ERROR("InternalADC not initialized!"); return 0; }
    if (channel == 0xFF) return 0;
    if (oversamplingCount_ <= 1) return analogRead(channel);
    
    uint32_t sum = 0;
    for (uint16_t i = 0; i < oversamplingCount_; i++) sum += analogRead(channel);
    return (uint16_t)(sum / oversamplingCount_);
}

float InternalADC::readVoltage() {
    return readVoltage(pin_);
}

float InternalADC::readVoltage(uint8_t channel, uint8_t gainOverride) {
    if (!initialized_) { LOG_ERROR("InternalADC not initialized!"); return 0.0f; }
    if (channel == 0xFF) return 0.0f;
    
    if (oversamplingCount_ <= 1) {
        return (float)analogRead(channel) * (ADC_VREF / (float)(ADC_MAX_VALUE + 1));
    }

    // ── Sample collection ──────────────────────────────────────────────────
    uint16_t samples[256];
    const uint16_t n = oversamplingCount_;
    for (uint16_t i = 0; i < n; i++) {
        samples[i] = static_cast<uint16_t>(analogRead(channel));
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

float InternalADC::readVoltageFast() {
    return readVoltageFast(pin_);
}

float InternalADC::readVoltageFast(uint8_t channel, uint8_t gainOverride) {
    if (!initialized_) { LOG_ERROR("InternalADC not initialized!"); return 0.0f; }
    if (channel == 0xFF) return 0.0f;
    
    // Fast path: just 2 samples averaged (no sorting, ~micro-seconds)
    uint32_t sum = analogRead(channel) + analogRead(channel);
    return ((float)sum / 2.0f) / ADC_MAX_VALUE * ADC_VREF;
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
    : i2cAddr_(i2cAddr), maxVoltage_(maxVoltage) {
    snprintf(name_, sizeof(name_), "MCP4725 0x%02X", i2cAddr);
}

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

    // Convert voltage → 12-bit DAC code using the runtime VDD reference
    currentValue_ = static_cast<uint16_t>((voltage / extDacVref_) * EXT_DAC_MAX_VALUE);
    if (currentValue_ > EXT_DAC_MAX_VALUE) currentValue_ = EXT_DAC_MAX_VALUE;

    mcp_.setVoltage(currentValue_, false);  // Write to DAC register, not EEPROM
}

void ExternalDAC::setExtDacVref(float vref) {
    constexpr float MIN_VDD = 4.0f;
    constexpr float MAX_VDD = 5.5f;
    if (vref < MIN_VDD) { LOG_WARN("ExternalDAC VDD clamped from %.2f to %.2f", vref, MIN_VDD); vref = MIN_VDD; }
    if (vref > MAX_VDD) { LOG_WARN("ExternalDAC VDD clamped from %.2f to %.2f", vref, MAX_VDD); vref = MAX_VDD; }
    extDacVref_ = vref;
    LOG_INFO("ExternalDAC 0x%02X VDD set to %.3f V (%.3f mV/step)",
             i2cAddr_, extDacVref_, (extDacVref_ / EXT_DAC_MAX_VALUE) * 1000.0f);
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
    snprintf(name_, sizeof(name_), "ADS1115 0x%02X", i2cAddr);
    if (oversamplingCount_ < 1)   oversamplingCount_ = 1;
    if (oversamplingCount_ > 256) oversamplingCount_ = 256;
}

bool ExternalADC::begin() {
    if (initialized_) { LOG_WARN("ExternalADC (0x%02X) already initialized", i2cAddr_); return true; }
    if (!ads_.begin(i2cAddr_)) {
        LOG_ERROR("ExternalADC ADS1115 not found at I2C addr 0x%02X", i2cAddr_);
        return false;
    }
    // GAIN_TWOTHIRDS: ±6.144 V FSR → 187.5 µV/LSB
    // This is the default to avoid saturation (especially for A1 / A2)
    ads_.setGain(GAIN_TWOTHIRDS);
    fsr_ = 6.144f;
    currentGainCode_ = 0;
    // 860 SPS: fastest rate → ~1.16 ms/sample (vs 7.8 ms at default 128 SPS)
    // With 64 oversampling samples: ~74 ms/point (vs ~500 ms at 128 SPS)
    ads_.setDataRate(RATE_ADS1115_860SPS);
    initialized_ = true;
    LOG_INFO("ExternalADC ADS1115 initialized at 0x%02X (16-bit, GAIN_SIXTEEN, %d samples, ~%.1f ENOB)",
             i2cAddr_, oversamplingCount_, getEffectiveBits());
    return true;
}

uint16_t ExternalADC::readRaw() {
    return readRaw(0); // Default channel A0
}

uint16_t ExternalADC::readRaw(uint8_t channel) {
    if (!initialized_) { LOG_ERROR("ExternalADC 0x%02X not initialized!", i2cAddr_); return 0; }
    int16_t raw = ads_.readADC_SingleEnded(channel);
    return (raw < 0) ? 0 : static_cast<uint16_t>(raw);
}

float ExternalADC::readVoltage() {
    return readVoltage(0, currentGainCode_);
}

float ExternalADC::readVoltage(uint8_t channel, uint8_t gainOverride) {
    if (!initialized_) { LOG_ERROR("ExternalADC 0x%02X not initialized!", i2cAddr_); return 0.0f; }

    bool autoRange = (gainOverride == 255);
    if (autoRange) {
        // Prime the ADC and establish the best gain dynamically
        readVoltageFast(channel, 255);
    }

    static uint8_t lastChannel = 255;
    bool configChanged = false;

    // ── Apply temporary gain ───────────────────────────────────────────────
    uint8_t gCode = autoRange ? lastAutoGain_[channel] : gainOverride;
    
    if (gCode != currentGainCode_) {
        setGain(gCode, true);
        configChanged = true;
    }

    if (channel != lastChannel) {
        configChanged = true;
        lastChannel = channel;
    }

    // Se o multiplexador mudou a porta ou o ganho, o ADC precisa jogar 1 leitura fora
    if (configChanged) {
        // Discard 1st conversion (wait at least 1-2 ms for electrical settling internally)
        delay(2);
        ads_.readADC_SingleEnded(channel);
    }

    // ── Sample collection ──────────────────────────────────────────────────
    uint16_t samples[256];
    const uint16_t n = oversamplingCount_;
    for (uint16_t i = 0; i < n; i++) {
        int16_t raw = ads_.readADC_SingleEnded(channel);
        samples[i] = (raw < 0) ? 0 : static_cast<uint16_t>(raw);
    }

    // ── Insertion Sort ─────────────────────────────────────────────────────
    if (n > 1) {
        for (uint16_t i = 1; i < n; i++) {
            uint16_t key = samples[i];
            int16_t  j   = static_cast<int16_t>(i) - 1;
            while (j >= 0 && samples[j] > key) { samples[j + 1] = samples[j]; j--; }
            samples[j + 1] = key;
        }
    }

    // ── Trimmed Mean (10% / 10%) ───────────────────────────────────────────
    const uint16_t trim  = (n > 5) ? (n / 10) : 0;
    const uint16_t start = trim;
    const uint16_t end   = n - trim;
    uint32_t sum = 0; uint16_t count = 0;
    for (uint16_t i = start; i < end; i++) { sum += samples[i]; count++; }
    if (count == 0) count = 1;

    // ── Restore permanent gain ─────────────────────────────────────────────
    // Removed because we rely on state tracking via currentGainCode_


    const float avgRaw = static_cast<float>(sum) / count;
    return avgRaw * (fsr_ / static_cast<float>(EXT_ADC_MAX_RAW));
}

float ExternalADC::readVoltageFast() {
    return readVoltageFast(0, currentGainCode_);
}

float ExternalADC::readVoltageFast(uint8_t channel, uint8_t gainOverride) {
    if (!initialized_) { LOG_ERROR("ExternalADC 0x%02X not initialized!", i2cAddr_); return 0.0f; }

    static uint8_t lastChannel = 255;
    bool configChanged = false;

    bool autoRange = (gainOverride == 255);
    uint8_t gCode = autoRange ? lastAutoGain_[channel] : gainOverride;

    if (gCode != currentGainCode_ && gCode != 255) {
        setGain(gCode, true);
        configChanged = true;
    }

    if (channel != lastChannel) {
        configChanged = true;
        lastChannel = channel;
    }

    if (configChanged) {
        delay(2);
        ads_.readADC_SingleEnded(channel);
    }

    int16_t r1 = ads_.readADC_SingleEnded(channel);

    // Auto-Ranging Logic
    if (autoRange) {
        bool changed = false;
        // Saturation downscale (FSR is too small, reading hit the absolute ceiling)
        while ((r1 > 31000 || r1 < -31000) && gCode > 0) {
            if (gCode == 16) gCode = 8;
            else if (gCode == 8) gCode = 4;
            else if (gCode == 4) gCode = 2;
            else if (gCode == 2) gCode = 1;
            else if (gCode == 1) gCode = 0;
            
            setGain(gCode, true);
            delay(2);
            ads_.readADC_SingleEnded(channel); // Dummy
            r1 = ads_.readADC_SingleEnded(channel);
            changed = true;
        }
        
        // Low signal upscale (FSR is too big, losing precision)
        while (abs(r1) < 14000 && gCode < 16 && !changed) {
            uint8_t next_g = gCode;
            float ratio = 2.0f;
            if (gCode == 0) { next_g = 1; ratio = 1.5f; }
            else if (gCode == 1) { next_g = 2; }
            else if (gCode == 2) { next_g = 4; }
            else if (gCode == 4) { next_g = 8; }
            else if (gCode == 8) { next_g = 16; }
            
            if (abs(r1) * ratio > 30000) break; // Unsafe to increase
            
            gCode = next_g;
            setGain(gCode, true);
            delay(2);
            ads_.readADC_SingleEnded(channel); // Dummy
            r1 = ads_.readADC_SingleEnded(channel);
            changed = true;
        }
        
        if (changed) {
            lastAutoGain_[channel] = gCode;
            LOG_INFO("[AUTO-GAIN] Ch%d: Switched to code %d (FSR = \xc2\xb1%.3f V)", channel, gCode, fsr_);
        }
    }

    // Fast path: just 2 samples averaged (no sorting, ~micro-seconds overhead after conversion)
    int16_t r2 = ads_.readADC_SingleEnded(channel);
    
    uint32_t sum = ((r1 < 0) ? 0 : r1) + ((r2 < 0) ? 0 : r2);
    float avgRaw = static_cast<float>(sum) / 2.0f;
    return avgRaw * (fsr_ / static_cast<float>(EXT_ADC_MAX_RAW));
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

void ExternalADC::setGain(uint8_t gainCode, bool silent) {
    adsGain_t g;
    float fsr;
    switch (gainCode) {
        case  0: g = GAIN_TWOTHIRDS; fsr = 6.144f; break;
        case  1: g = GAIN_ONE;       fsr = 4.096f; break;
        case  2: g = GAIN_TWO;       fsr = 2.048f; break;
        case  4: g = GAIN_FOUR;      fsr = 1.024f; break;
        case  8: g = GAIN_EIGHT;     fsr = 0.512f; break;
        case 16: g = GAIN_SIXTEEN;   fsr = 0.256f; break;
        default:
            LOG_ERROR("Invalid gain code %d, defaulting to ±0.256V", gainCode);
            g = GAIN_SIXTEEN; fsr = 0.256f;
            break;
    }
    ads_.setGain(g);
    fsr_ = fsr;
    currentGainCode_ = gainCode;
    
    if (!silent) {
        LOG_INFO("ExternalADC gain set: code=%d, FSR=±%.3f V (%.4f mV/LSB)",
                 gainCode, fsr, (fsr / EXT_ADC_MAX_RAW) * 1000.0f);
    }
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
    LOG_WARN("--- HARDWARE DEVICE SELECTION (INTERNAL MODE) ---");
    LOG_WARN("PROVISIONAL OVERRIDE: Forcing InternalDAC for VDS (Drain).");
    // VDS DAC — InternalDAC channel 1 (GPIO25)
    auto vds = std::unique_ptr<InternalDAC>(new InternalDAC(1, config.max_vds));
    vds->begin();
    dacVDS_ = std::move(vds);

    LOG_WARN("PROVISIONAL OVERRIDE: Forcing ExternalDAC (MCP4725) for VGS (Gate) even in INTERNAL mode.");
    // VGS DAC — ExternalDAC MCP4725 (I2C 0x60, ADDR→GND)
    auto vgs = std::unique_ptr<ExternalDAC>(new ExternalDAC(EXT_DAC_VGS_ADDR, config.max_vgs));
    if (!vgs->begin()) {
        LOG_ERROR("ExternalDAC (MCP4725 VGS) init failed — falling back to InternalDAC for VGS");
        auto vgs_fallback = std::unique_ptr<InternalDAC>(new InternalDAC(2, config.max_vgs));
        vgs_fallback->begin();
        dacVGS_ = std::move(vgs_fallback);
    } else {
        dacVGS_ = std::move(vgs);
    }

    LOG_INFO("Using InternalADC for Shunt (Current limits).");
    // Shunt ADC — InternalADC (GPIO34)
    auto adc = std::unique_ptr<InternalADC>(new InternalADC(config.adc_shunt_pin, config.adc_oversampling));
    adc->begin();
    adcShunt_ = std::move(adc);

    LOG_INFO("  [INTERNAL] VDS: InternalDAC GPIO%d (8-bit, %.1f mV/step) [PROVISIONAL]",
             DAC_VDS_PIN, dacVDS_->getResolution() * 1000.0f);
    LOG_INFO("  [INTERNAL] VGS: ExternalDAC  MCP4725 0x%02X (12-bit, %.3f mV/step) [PROVISIONAL]",
             EXT_DAC_VGS_ADDR, dacVGS_->getResolution() * 1000.0f);
    LOG_INFO("  [INTERNAL] ADC: InternalADC  GPIO%d (12-bit, %d samples)",
             config.adc_shunt_pin, config.adc_oversampling);
}

void HardwareHAL::initExternal(const HalConfig& config) {
    LOG_WARN("--- HARDWARE DEVICE SELECTION (EXTERNAL MODE) ---");
    
    // VDS DAC — ExternalDAC MCP4725 (I2C 0x61, ADDR→VCC) — NEW in v8.0.5
    auto vds = std::unique_ptr<ExternalDAC>(new ExternalDAC(EXT_DAC_VDS_ADDR, config.max_vds));
    if (!vds->begin()) {
        LOG_ERROR("ExternalDAC (MCP4725 VDS 0x61) init failed — falling back to InternalDAC for VDS");
        auto vds_fallback = std::unique_ptr<InternalDAC>(new InternalDAC(1, config.max_vds));
        vds_fallback->begin();
        dacVDS_ = std::move(vds_fallback);
    } else {
        dacVDS_ = std::move(vds);
    }

    LOG_WARN("Using ExternalDAC (MCP4725) for VGS (Gate) as requested by external mode.");
    // VGS DAC — ExternalDAC MCP4725 (I2C 0x60, ADDR→GND)
    auto vgs = std::unique_ptr<ExternalDAC>(new ExternalDAC(EXT_DAC_VGS_ADDR, config.max_vgs));
    if (!vgs->begin()) {
        LOG_ERROR("ExternalDAC (MCP4725 VGS) init failed — falling back to InternalDAC for VGS");
        auto vgs_fallback = std::unique_ptr<InternalDAC>(new InternalDAC(2, config.max_vgs));
        vgs_fallback->begin();
        dacVGS_ = std::move(vgs_fallback);
    } else {
        dacVGS_ = std::move(vgs);
    }

    LOG_INFO("Using ExternalADC (ADS1115) for Shunt.");
    // Shunt ADC — ExternalADC ADS1115 (I2C 0x48, channel A0)
    auto adc = std::unique_ptr<ExternalADC>(new ExternalADC(EXT_ADC_ADDR, config.adc_oversampling));
    if (!adc->begin()) {
        LOG_ERROR("ExternalADC (ADS1115) init failed — falling back to InternalADC");
        auto adc_fallback = std::unique_ptr<InternalADC>(new InternalADC(config.adc_shunt_pin, config.adc_oversampling));
        adc_fallback->begin();
        adcShunt_ = std::move(adc_fallback);
    } else {
        adcShunt_ = std::move(adc);
    }

    LOG_INFO("  [EXTERNAL] VDS: InternalDAC  GPIO%d (8-bit, %.1f mV/step) [PROVISIONAL]",
             DAC_VDS_PIN, dacVDS_->getResolution() * 1000.0f);
    LOG_INFO("  [EXTERNAL] VGS: ExternalDAC  MCP4725 0x%02X (12-bit, %.3f mV/step)",
             EXT_DAC_VGS_ADDR, dacVGS_->getResolution() * 1000.0f);
    LOG_INFO("  [EXTERNAL] ADC: ExternalADC  ADS1115 0x%02X (16-bit, %d samples, ~%.1f ENOB)",
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

String HardwareHAL::getHardwareSummary() const {
    char buf[128];
    snprintf(buf, sizeof(buf), "Hardware: %s (VDS: %s, VGS: %s, ADC: %s)",
             (currentMode_ == HardwareMode::HW_EXTERNAL) ? "External Path" : "Internal Path",
             dacVDS_ ? dacVDS_->getName() : "None",
             dacVGS_ ? dacVGS_->getName() : "None",
             adcShunt_ ? adcShunt_->getName() : "None");
    return String(buf);
}

float HardwareHAL::readShuntVoltage(uint8_t gainCode) {
    return adcShunt_->readVoltage(0, gainCode); // A0 mapped to Shunt
}

float HardwareHAL::readVD_Actual(uint8_t gainCode) {
    // Assuming external routing via ADS1115 for VDS at A1
    return adcShunt_->readVoltage(1, gainCode); 
}

float HardwareHAL::readVG_Actual(uint8_t gainCode) {
    // Assuming external routing via ADS1115 for VGS at A2
    return adcShunt_->readVoltage(2, gainCode);
}

float HardwareHAL::readShuntVoltageFast(uint8_t gainCode) {
    return adcShunt_->readVoltageFast(0, gainCode);
}

float HardwareHAL::readVD_ActualFast(uint8_t gainCode) {
    return adcShunt_->readVoltageFast(1, gainCode);
}

float HardwareHAL::readVG_ActualFast(uint8_t gainCode) {
    return adcShunt_->readVoltageFast(2, gainCode);
}

ExternalDAC* HardwareHAL::getExternalVGS() {
    // Dynamic cast not available with -fno-rtti; use a flag instead.
    if (currentMode_ == HardwareMode::HW_EXTERNAL ||
        currentMode_ == HardwareMode::HW_INTERNAL) {
        // dacVGS_ is an ExternalDAC in both modes (due to PROVISIONAL override)
        return static_cast<ExternalDAC*>(dacVGS_.get());
    }
    return nullptr;
}

void HardwareHAL::setADC_Gain(uint8_t gainCode) {
    adcShunt_->setGain(gainCode);
}

// ============================================================================
// Legacy Compatibility Functions
// ============================================================================

void init()                   { HardwareHAL::instance().begin(); }
void setVDS(float voltage)    { HardwareHAL::instance().getVDS().setVoltage(voltage); }
void setVGS(float voltage)    { HardwareHAL::instance().getVGS().setVoltage(voltage); }
float readShuntVoltage(uint8_t gainCode)      { return HardwareHAL::instance().readShuntVoltage(gainCode); }
float readVD_Actual(uint8_t gainCode)         { return HardwareHAL::instance().readVD_Actual(gainCode); }
float readVG_Actual(uint8_t gainCode)         { return HardwareHAL::instance().readVG_Actual(gainCode); }
float readShuntVoltageFast(uint8_t gainCode)  { return HardwareHAL::instance().readShuntVoltageFast(gainCode); }
float readVD_ActualFast(uint8_t gainCode)     { return HardwareHAL::instance().readVD_ActualFast(gainCode); }
float readVG_ActualFast(uint8_t gainCode)     { return HardwareHAL::instance().readVG_ActualFast(gainCode); }
void setADC_Gain(uint8_t g)   { HardwareHAL::instance().setADC_Gain(g); }
void shutdown()               { HardwareHAL::instance().shutdown(); }

} // namespace hal
