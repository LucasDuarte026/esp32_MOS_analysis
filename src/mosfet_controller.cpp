#include "mosfet_controller.h"
#include "hardware_hal.h"
#include "file_manager.h"
#include "led_status.h"
#include "math_engine.h"
#include "version.h"
#include <sys/time.h>
#include <time.h>
#include <cstdio>


// Buffer size for batch writing (2KB chunks)
static const size_t WRITE_BUFFER_SIZE = 2048;

MOSFETController::MOSFETController()
{
}

MOSFETController::~MOSFETController()
{
    if (mutex_) {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

void MOSFETController::begin()
{
    mutex_ = xSemaphoreCreateMutex();
    if (!mutex_) {
        LOG_ERROR("Failed to create MOSFET mutex");
    }
    
    // Initialize hardware abstraction layer (DACs and ADC)
    hal::init();
    
    LOG_INFO("MOSFET Controller initialized");
}

bool MOSFETController::openMeasurementFile() {
    String basename = config_.filename;
    if (basename.endsWith(".csv")) basename = basename.substring(0, basename.length() - 4);
    
    time_t now;
    time(&now);
    String finalFilename = basename + "_" + String(now) + ".csv";
    
    currentFilename_ = finalFilename; // Save for potential deletion
    
    if (!FileManager::isValidFilename(finalFilename)) {
        LOG_ERROR("Invalid filename rejected: %s", finalFilename.c_str());
        return false;
    }

    String path = String(FileManager::MEASUREMENTS_DIR) + "/" + finalFilename;
    
    // Use FFat filesystem
    currentFile_ = FFat.open(path, "w");
    
    if (!currentFile_) {
        LOG_ERROR("Failed to open file for writing: %s", path.c_str());
        return false;
    }
    
    currentFile_.println("timestamp,vds,vgs,vsh_measured,ids,gm,vt,ss");
    
    LOG_INFO("Streaming data to: %s", path.c_str());
    return true;
}

void MOSFETController::closeMeasurementFile() {
    LOG_DEBUG("closeMeasurementFile called, currentFile_ valid: %s", currentFile_ ? "YES" : "NO");
    if (currentFile_) {
        size_t fileSize = currentFile_.size();
        LOG_DEBUG("File size before close: %u bytes", (unsigned)fileSize);
        currentFile_.flush();  // Ensure all data is written
        currentFile_.close();
        LOG_INFO("File closed successfully (size: %u bytes)", (unsigned)fileSize);
    } else {
        LOG_WARN("closeMeasurementFile: currentFile_ was not open!");
    }
}

bool MOSFETController::startMeasurementAsync(const SweepConfig& config)
{
    if (mutex_ && xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        LOG_ERROR("Failed to acquire mutex");
        return false;
    }
    
    if (measuring_) {
        LOG_WARN("Measurement already in progress");
        if (mutex_) xSemaphoreGive(mutex_);
        return false;
    }
    
    if (config.vgs_start < 0 || config.vgs_end > 5.0 || config.vds_end > 5.0) {
        LOG_ERROR("Invalid Voltage range");
        if (mutex_) xSemaphoreGive(mutex_);
        return false;
    }
    
    config_ = config;
    
    // Generate filename but DO NOT open file yet
    String basename = config_.filename;
    if (basename.endsWith(".csv")) basename = basename.substring(0, basename.length() - 4);
    
    time_t now; 
    time(&now);
    currentFilename_ = basename + "_" + String((unsigned long)now) + ".csv";
    
    if (!FileManager::isValidFilename(currentFilename_)) {
        LOG_ERROR("Invalid filename generated: %s", currentFilename_.c_str());
        if (mutex_) xSemaphoreGive(mutex_);
        return false;
    }
    
    LOG_INFO("Generated filename: %s", currentFilename_.c_str());
    
    measuring_ = true;
    cancelled_ = false; // Reset cancel flag
    hasError_ = false;  // Reset error state
    errorMessage_ = "";
    currentVds_ = config.vds_start;
    progressPercent_ = 0;
    
    BaseType_t res = xTaskCreatePinnedToCore(
        measurementTaskWrapper, 
        "MOS_Measure", 
        8192, 
        this, 
        1, 
        &taskHandle_,
        1  // Pin to Core 1
    );

    if (res != pdPASS) {
        LOG_ERROR("Failed to create measurement task");
        measuring_ = false;
        if (mutex_) xSemaphoreGive(mutex_);
        return false;
    }
    
    LOG_INFO("Starting measurement SWEEP (Async)");
    LOG_INFO("  VDS: %.2fV-%.2fV | VGS: %.2fV-%.2fV | Rshunt: %.2fΩ", 
             config.vds_start, config.vds_end, 
             config.vgs_start, config.vgs_end, config_.rshunt);
    
    // Set LED to measuring pattern
    led_status::setState(led_status::State::MEASURING);
             
    if (mutex_) xSemaphoreGive(mutex_);
    return true;
}

void MOSFETController::measurementTaskWrapper(void* param)
{
    MOSFETController* controller = static_cast<MOSFETController*>(param);
    if (controller) {
        controller->performSweep();
        
        // CRITICAL: Close file ONLY here, after sweep is fully complete
        controller->closeMeasurementFile();
        
        // Clean up
        controller->measuring_ = false;
        controller->taskHandle_ = nullptr;
        
        // Return LED to standby pattern
        led_status::setState(led_status::State::STANDBY);
        
        LOG_INFO("Async Measurement Task Finished");
    }
    vTaskDelete(nullptr);
}

void MOSFETController::cancelMeasurement() {
    if (!measuring_) {
        LOG_WARN("No measurement to cancel");
        return;
    }
    
    cancelled_ = true;
    LOG_WARN("Cancelling measurement...");
    
    // Wait a bit for the task to notice
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Close and delete the incomplete file
    closeMeasurementFile();
    
    // Delete the partial file
    if (!currentFilename_.isEmpty()) {
        bool deleted = FileManager::deleteFile(currentFilename_);
        if (deleted) {
            LOG_INFO("Deleted incomplete file: %s", currentFilename_.c_str());
        }
    }
    
    measuring_ = false;
    LOG_INFO("Measurement cancelled");
}

bool MOSFETController::startMeasurement(const SweepConfig& config)
{
    return startMeasurementAsync(config);
}

void MOSFETController::stopMeasurement()
{
    cancelMeasurement();
}

bool MOSFETController::isMeasuring() const
{
    return measuring_;
}

void MOSFETController::reset()
{
    if (measuring_) {
        cancelMeasurement();
    }
    results_buffer_.clear();
    currentVds_ = 0;
    progressPercent_ = 0;
}

MOSFETController::ProgressStatus MOSFETController::getProgress() const
{
    ProgressStatus status;
    status.is_running = measuring_;
    status.current_vds = currentVds_;
    status.progress_percent = progressPercent_;
    status.has_error = hasError_;
    status.error_message = errorMessage_;
    
    if (!measuring_) {
        if (hasError_) {
            status.message = "Error: " + errorMessage_;
        } else {
            status.message = "Idle";
        }
    } else if (cancelled_) {
        status.message = "Cancelling...";
    } else {
        char buf[64];
        snprintf(buf, sizeof(buf), "Measuring VDS = %.2fV", currentVds_);
        status.message = String(buf);
    }
    
    return status;
}

float MOSFETController::readAnalogVoltage()
{
    // Delegate to HAL for averaged reading
    return hal::readShuntVoltage();
}

// ============================================================================
// Closed-loop DAC calibration helpers
// ============================================================================
//
// Strategy ("VDS/VGS differential"):
//   The error is computed on the ACTUAL differential transistor voltage:
//     VDS_meas = VD_read - VSH
//     VGS_meas = VG_read - VSH
//   VSH is re-read on every iteration so the correction automatically
//   compensates for the current-dependent shunt drop.
//
//   1. Set DAC to initial probe, wait for rail to settle.
//   2. Read VSH + VD/VG, compute differential error.
//   3. If |error| <= threshold → done.
//   4. Nudge probe by error, clamp, repeat up to DAC_CALIB_MAX_ITER.
//   5. Emit WARN and return best estimate on timeout.

float MOSFETController::calibrateVDS(float target_vds, int settling_ms)
{
    // Initial probe: use target + current shunt drop as better guess
    float vsh_init = hal::readShuntVoltageFast(config_.adc_gain_vsh);
    float probe = target_vds + vsh_init;
    hal::setVDS(probe);
    vTaskDelay(pdMS_TO_TICKS(settling_ms));

    float vd_read  = hal::readVD_ActualFast(config_.adc_gain_vd);
    float vsh      = hal::readShuntVoltageFast(config_.adc_gain_vsh); // Update VSH immediately after VD
    float vds_meas = vd_read - vsh;
    float error    = target_vds - vds_meas;

    // Fast path: differential already within tolerance
    if (fabsf(error) <= VDS_GLOBAL_ERROR) {
        return probe;
    }

    // Slow path: iterative correction on VDS differential
    for (int iter = 1; iter < DAC_CALIB_MAX_ITER; iter++) {
        probe += error;  // nudge DAC probe toward target
        if (probe < 0.0f) probe = 0.0f;
        if (probe > 5.5f) probe = 5.5f;

        hal::setVDS(probe);
        vTaskDelay(pdMS_TO_TICKS(settling_ms));

        vd_read  = hal::readVD_ActualFast(config_.adc_gain_vd);
        vsh      = hal::readShuntVoltageFast(config_.adc_gain_vsh); // Update VSH immediately after VD
        vds_meas = vd_read - vsh;
        error    = target_vds - vds_meas;

        if (fabsf(error) <= VDS_GLOBAL_ERROR) {
            LOG_DEBUG("[CALIB VDS] target=%6.3f converged in %2d iter(s), probe=%6.3f vds_meas=%7.4f err=%+7.4f",
                      target_vds, iter + 1, probe, vds_meas, error);
            return probe;
        }
    }

    // Final refresh before logging failure
    vd_read  = hal::readVD_Actual(config_.adc_gain_vd);
    vsh      = hal::readShuntVoltage(config_.adc_gain_vsh);
    vds_meas = vd_read - vsh;
    error    = target_vds - vds_meas;

    // Extract what the Auto-Ranging actually chose at the end for debug purposes
    uint8_t gain_vd = hal::HardwareHAL::instance().getShuntADC().getLastUsedGain(1);
    uint8_t gain_vsh = hal::HardwareHAL::instance().getShuntADC().getLastUsedGain(0);

    LOG_WARN("[CALIB VDS] FAILED | Target: %5.3fV | Meas: %5.3fV | Err: %6.4fV | Probe: %5.3fV | VD: %5.3fV | VS: %5.3fV | Gains: %d/%d",
             target_vds, vds_meas, error, probe, vd_read, vsh, gain_vd, gain_vsh);
    return probe;
}

float MOSFETController::calibrateVGS(float target_vgs, int settling_ms)
{
    // Initial probe: use target + current shunt drop as better guess
    float vsh_init = hal::readShuntVoltageFast(config_.adc_gain_vsh);
    float probe = target_vgs + vsh_init;
    hal::setVGS(probe);
    vTaskDelay(pdMS_TO_TICKS(settling_ms));

    float vg_read  = hal::readVG_ActualFast(config_.adc_gain_vg);
    float vsh      = hal::readShuntVoltageFast(config_.adc_gain_vsh); // Update VSH immediately after VG
    float vgs_meas = vg_read - vsh;
    float error    = target_vgs - vgs_meas;

    // Fast path
    if (fabsf(error) <= VGS_GLOBAL_ERROR) {
        return probe;
    }

    // Slow path: iterative correction on VGS differential
    for (int iter = 1; iter < DAC_CALIB_MAX_ITER; iter++) {
        probe += error;
        if (probe < 0.0f) probe = 0.0f;
        if (probe > 5.5f) probe = 5.5f;

        hal::setVGS(probe);
        vTaskDelay(pdMS_TO_TICKS(settling_ms));

        vg_read  = hal::readVG_ActualFast(config_.adc_gain_vg);
        vsh      = hal::readShuntVoltageFast(config_.adc_gain_vsh); // Update VSH immediately after VG
        vgs_meas = vg_read - vsh;
        error    = target_vgs - vgs_meas;

        if (fabsf(error) <= VGS_GLOBAL_ERROR) {
            LOG_DEBUG("[CALIB VGS] target=%6.3f converged in %2d iter(s), probe=%6.3f vgs_meas=%7.4f err=%+7.4f",
                      target_vgs, iter + 1, probe, vgs_meas, error);
            return probe;
        }
    }

    // Final refresh before logging failure
    vg_read  = hal::readVG_Actual(config_.adc_gain_vg);
    vsh      = hal::readShuntVoltage(config_.adc_gain_vsh);
    vgs_meas = vg_read - vsh;
    error    = target_vgs - vgs_meas;

    // Extract what the Auto-Ranging actually chose at the end for debug purposes
    uint8_t gain_vg = hal::HardwareHAL::instance().getShuntADC().getLastUsedGain(2);
    uint8_t gain_vsh = hal::HardwareHAL::instance().getShuntADC().getLastUsedGain(0);

    LOG_WARN("[CALIB VGS] FAILED | Target: %5.3fV | Meas: %5.3fV | Err: %6.4fV | Probe: %5.3fV | VG: %5.3fV | VS: %5.3fV | Gains: %d/%d",
             target_vgs, vgs_meas, error, probe, vg_read, vsh, gain_vg, gain_vsh);
    return probe;
}


void MOSFETController::performSweep()
{
    // STREAMING VERSION: Write data directly to file, no memory accumulation
    
    const float vds_start = config_.vds_start;
    const float vds_end = config_.vds_end;
    const float vds_step = config_.vds_step;
    const float vgs_start = config_.vgs_start;
    const float vgs_end = config_.vgs_end;
    const float vgs_step = config_.vgs_step;
    const float rshunt = config_.rshunt;
    const int settling = config_.settling_ms;
    const bool sweepVDS = (config_.sweep_mode == SWEEP_VDS);
    
    // Calculate total steps for progress
    int outer_steps, inner_steps;
    if (sweepVDS) {
        outer_steps = (int)roundf((vgs_end - vgs_start) / vgs_step) + 1;
        inner_steps = (int)roundf((vds_end - vds_start) / vds_step) + 1;
    } else {
        outer_steps = (int)roundf((vds_end - vds_start) / vds_step) + 1;
        inner_steps = (int)roundf((vgs_end - vgs_start) / vgs_step) + 1;
    }
    int total_points = outer_steps * inner_steps;
    int current_point = 0;
    
    // Open file and write header FIRST
    String path = String(FileManager::MEASUREMENTS_DIR) + "/" + currentFilename_;
    LOG_DEBUG("Opening file for streaming: %s", path.c_str());
    currentFile_ = FFat.open(path.c_str(), FILE_WRITE);
    if (!currentFile_) {
        LOG_ERROR("Failed to open file for streaming: %s", path.c_str());
        hasError_ = true;
        errorMessage_ = "Failed to open file";
        return;
    }
    LOG_DEBUG("File opened successfully. Handle valid: %s", currentFile_ ? "YES" : "NO");
    
    // Write header
    char lineBuf[256];
    int len;
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# MOSFET Characterization Data\n");
    currentFile_.write((uint8_t*)lineBuf, len);
    
    time_t now; time(&now);
    struct tm* timeinfo = localtime(&now);
    len = snprintf(lineBuf, sizeof(lineBuf), "# Date: %04d-%02d-%02d %02d:%02d:%02d\n",
        timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
        timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    currentFile_.write((uint8_t*)lineBuf, len);
    
    // SWEEP MODE FLAG - critical for visualization
    len = snprintf(lineBuf, sizeof(lineBuf), "# Sweep Mode: %s\n", sweepVDS ? "VDS" : "VGS");
    currentFile_.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# Rshunt: %.3f Ohms\n", config_.rshunt);
    currentFile_.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# VDS Range: %.3f to %.3f V (step %.3f)\n",
        config_.vds_start, config_.vds_end, config_.vds_step);
    currentFile_.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# VGS Range: %.3f to %.3f V (step %.3f)\n",
        config_.vgs_start, config_.vgs_end, config_.vgs_step);
    currentFile_.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# Settling Time: %d ms\n", config_.settling_ms);
    currentFile_.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# Oversampling: %s (%dx)\n", 
        config_.oversampling > 1 ? "enabled" : "disabled", config_.oversampling);
    currentFile_.write((uint8_t*)lineBuf, len);

    len = snprintf(lineBuf, sizeof(lineBuf), "# Sweep Mode: %s\n", sweepVDS ? "VDS" : "VGS");
    currentFile_.write((uint8_t*)lineBuf, len);

    auto getGainLabel = [](uint8_t g) -> const char* {
        switch (g) {
            case  0: return "GAIN_TWOTHIRDS (±6.144 V)";
            case  1: return "GAIN_ONE (±4.096 V)";
            case  2: return "GAIN_TWO (±2.048 V)";
            case  4: return "GAIN_FOUR (±1.024 V)";
            case  8: return "GAIN_EIGHT (±0.512 V)";
            case 16: return "GAIN_SIXTEEN (±0.256 V)";
            case 255: return "AUTO (Dynamic Otimize)";
            default: return "GAIN_SIXTEEN (±0.256 V)";
        }
    };
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# ADC Gains: VSh=%s, VD=%s, VG=%s\n", 
                   getGainLabel(config_.adc_gain_vsh), 
                   getGainLabel(config_.adc_gain_vd), 
                   getGainLabel(config_.adc_gain_vg));
    currentFile_.write((uint8_t*)lineBuf, len);

    // Hardware mode metadata — records reality-based peripheral identification
    len = snprintf(lineBuf, sizeof(lineBuf), "# %s\n", hal::HardwareHAL::instance().getHardwareSummary().c_str());
    currentFile_.write((uint8_t*)lineBuf, len);

    len = snprintf(lineBuf, sizeof(lineBuf), "# Firmware: %s\n", SOFTWARE_VERSION);
    currentFile_.write((uint8_t*)lineBuf, len);
    
    // Configure ADC gain for shunt measurement is now applied dynamically per read
    // if (config_.use_external_hw) {
    //    // hal::setADC_Gain(config_.adc_gain);
    // }

    // Column Headers — v9.0.12+: added vsh_precise (A3)
    len = snprintf(lineBuf, sizeof(lineBuf), "#\ntimestamp,vd,vg,vd_read,vg_read,vsh,vsh_precise,vds_true,vgs_true,ids\n");
    currentFile_.write((uint8_t*)lineBuf, len);
    currentFile_.flush();
    
    LOG_INFO("Starting %s sweep - Oversampling: %s (%dx), Settling: %dms, GainCode: %d", 
        sweepVDS ? "VD" : "VG",
        config_.oversampling > 1 ? "ON" : "OFF", 
        config_.oversampling, 
        config_.settling_ms,
        config_.adc_gain_vsh);
    
    int rowCount = 0;
    
    // Temporary buffer for one curve (cleared after each outer loop iteration)
    CurveData currentCurve;
    
    // Mode: Id vs Vds sweep (outer = VGS fixed, inner = VDS swept)
    // −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
    // Calibration strategy for IdVds:
    //   - VGS does NOT change each inner step → calibrate once per outer curve,
    //     then do a fast verify-only check at every inner step (no full retry
    //     unless it drifted past threshold).
    //   - VDS sweeps every inner step → always calibrate fully before each read.
    if (sweepVDS) {
        for (int i_vgs = 0; i_vgs < outer_steps && measuring_ && !cancelled_; i_vgs++) {
            float vgs = vgs_start + i_vgs * vgs_step;
            currentVds_ = vgs; // Use for progress display (outer loop var)

            // Calibrate VGS once at the start of this curve (differential target)
            calibrateVGS(vgs, settling);

            for (int i_vds = 0; i_vds < inner_steps && measuring_ && !cancelled_; i_vds++) {
                float vds = vds_start + i_vds * vds_step;

                // VDS changes every step → full differential calibration every time
                calibrateVDS(vds, settling);

                // ── Verify BOTH axes before taking the measurement ──────────
                // VGS drift check: re-read differential and re-calibrate if needed
                {
                    float vsh_now  = hal::readShuntVoltage(config_.adc_gain_vsh);
                    float vgs_now  = hal::readVG_Actual(config_.adc_gain_vg) - vsh_now;
                    if (fabsf(vgs_now - vgs) > VGS_GLOBAL_ERROR) {
                        calibrateVGS(vgs, settling);
                    }
                }
                // VDS drift check (may have shifted after VGS re-calibration)
                {
                    float vsh_now  = hal::readShuntVoltage(config_.adc_gain_vsh);
                    float vds_now  = hal::readVD_Actual(config_.adc_gain_vd) - vsh_now;
                    if (fabsf(vds_now - vds) > VDS_GLOBAL_ERROR) {
                        calibrateVDS(vds, settling);
                    }
                }

                // ── Final verification and data acquisition ──────────
                float vd_actual = hal::readVD_Actual(config_.adc_gain_vd);
                float vg_actual = hal::readVG_Actual(config_.adc_gain_vg);
                float vsh_lowres = hal::readShuntVoltage(config_.adc_gain_vsh); 
                
                // Read amplified shunt (A3) for the new column and canonical calculations
                float vsh_precise = hal::readShuntVoltageAMPFast(config_.adc_gain_vsh);

                // Use the best available shunt for canonical IDs/TrueVoltages
                float vsh = (vsh_precise < 0.032f) ? vsh_precise : vsh_lowres;

                float vds_true  = vd_actual - vsh;
                float vgs_true  = vg_actual - vsh;
                float ids       = vsh / rshunt;

                currentFile_.printf("%lu,%.3f,%.3f,%.3f,%.3f,%.6f,%.6f,%.4f,%.4f,%.6e\n",
                           (unsigned long)millis(), vds, vgs,
                           vd_actual, vg_actual, vsh, vsh_precise,
                           vds_true, vgs_true, ids);

                rowCount++;
                current_point++;
                progressPercent_ = (current_point * 100) / total_points;

                if (rowCount % 50 == 0) {
                    currentFile_.flush();
                    vTaskDelay(1);
                }
            }

            currentFile_.flush();
            LOG_INFO("VGS=%6.3fV streamed. Rows: %d", vgs, rowCount);
        }

    } else {
        // Mode: Id vs Vgs sweep (outer = VDS fixed, inner = VGS swept) — default
        // −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
        // Calibration strategy for IdVgs (VDS/VGS differential targets):
        //   - VDS does NOT change each inner step → calibrate once per outer
        //     curve with 3× settling, then do a differential drift check before
        //     every measurement point.
        //   - VGS sweeps every inner step → always calibrate fully before each
        //     measurement point.
        //   - After every VGS calibration, BOTH VDS and VGS differentials are
        //     verified and corrected if needed before the ADC read.
        for (int i_vds = 0; i_vds < outer_steps && measuring_ && !cancelled_; i_vds++) {
            float vds = vds_start + i_vds * vds_step;
            currentVds_ = vds;

            // Reset curve buffer for this VDS value
            currentCurve = CurveData();
            currentCurve.vds = vds;
            currentCurve.rshunt = rshunt;

            // Calibrate VDS once for this curve (differential target, 3× settling)
            calibrateVDS(vds, settling * 3);

            for (int i_vgs = 0; i_vgs < inner_steps && measuring_ && !cancelled_; i_vgs++) {
                float vgs = vgs_start + i_vgs * vgs_step;

                // VGS changes every step → full differential calibration every time
                calibrateVGS(vgs, settling);

                // ── Verify BOTH axes before taking the measurement ──────────
                // VDS drift check: VDS may shift after VGS changes (coupling)
                {
                    float vsh_now = hal::readShuntVoltage(config_.adc_gain_vsh);
                    float vds_now = hal::readVD_Actual(config_.adc_gain_vd) - vsh_now;
                    if (fabsf(vds_now - vds) > VDS_GLOBAL_ERROR) {
                        calibrateVDS(vds, settling);
                    }
                }
                // VGS drift check: re-verify after VDS re-calibration
                {
                    float vsh_now = hal::readShuntVoltage(config_.adc_gain_vsh);
                    float vgs_now = hal::readVG_Actual(config_.adc_gain_vg) - vsh_now;
                    if (fabsf(vgs_now - vgs) > VGS_GLOBAL_ERROR) {
                        calibrateVGS(vgs, settling);
                    }
                }

                // Final read — both axes confirmed within tolerance
                uint32_t t_dac  = millis();
                uint32_t t_set  = millis();
                if (settling > 0) vTaskDelay(pdMS_TO_TICKS(settling));
                uint32_t t_adc  = millis();
                // ── Final verification and data acquisition ──────────
                float vd_actual = hal::readVD_Actual(config_.adc_gain_vd);
                float vg_actual = hal::readVG_Actual(config_.adc_gain_vg);
                float vsh_lowres = hal::readShuntVoltage(config_.adc_gain_vsh);

                // Read amplified shunt (A3) for the new column and canonical calculations
                float vsh_precise = hal::readShuntVoltageAMPFast(config_.adc_gain_vsh);

                // Use the best available shunt for canonical IDs/TrueVoltages
                float vsh = (vsh_precise < 0.032f) ? vsh_precise : vsh_lowres;

                float vds_true_val = vd_actual - vsh;
                float vgs_true_val = vg_actual - vsh;
                uint32_t t_done = millis();
                float ids       = vsh / rshunt;

                // Buffer data for parameter calculation
                currentCurve.vgs.push_back(vgs);
                currentCurve.ids.push_back(ids);
                currentCurve.vsh.push_back(vsh);
                currentCurve.vd_read.push_back(vd_actual);
                currentCurve.vg_read.push_back(vg_actual);
                currentCurve.vds_true.push_back(vds_true_val);
                currentCurve.vgs_true.push_back(vgs_true_val);
                currentCurve.vsh_precise.push_back(vsh_precise);
                currentCurve.timestamps.push_back(millis());

                currentFile_.printf("%lu,%.3f,%.3f,%.3f,%.3f,%.6f,%.6f,%.4f,%.4f,%.6e\n",
                           (unsigned long)millis(), vds, vgs,
                           vd_actual, vg_actual, vsh, vsh_precise,
                           vds_true_val, vgs_true_val, ids);

                rowCount++;
                current_point++;
                progressPercent_ = (current_point * 100) / total_points;

                if (rowCount % 50 == 1) {
                    LOG_DEBUG("[TIMING] VGS=%6.3fV | DAC=%4lums Settle=%4lums ADC=%4lums Total=%4lums | vds_true=%6.3f vgs_true=%6.3f",
                              vgs,
                              (unsigned long)(t_set  - t_dac),
                              (unsigned long)(t_adc  - t_set),
                              (unsigned long)(t_done - t_adc),
                              (unsigned long)(t_done - t_dac),
                              vds_true_val, vgs_true_val);
                }

                if (rowCount % 50 == 0) {
                    currentFile_.flush();
                    vTaskDelay(1);
                }
            }

            // Calculate parameters for this curve
            calculateCurveParams(currentCurve);

            currentFile_.printf("# VDS=%.3fV: Vt=%.3fV, SS=%.2f mV/dec, MaxGm=%.2e S, SS_Tangent_VGS:%.3f,%.3f SS_Tangent_LogId:%.3f,%.3f\n",
                       vds, currentCurve.vt, currentCurve.ss, currentCurve.max_gm,
                       currentCurve.ss_x1, currentCurve.ss_x2, currentCurve.ss_y1, currentCurve.ss_y2);

            currentFile_.flush();
            LOG_INFO("VDS=%6.3fV: Vt=%6.3f, SS=%6.1f mV/dec, MaxGm=%8.2e", vds, currentCurve.vt, currentCurve.ss, currentCurve.max_gm);
        }
    }
    
    // Final flush - close is handled by closeMeasurementFile()
    LOG_DEBUG("Before final flush: file valid=%s, size=%u, position=%u", 
              currentFile_ ? "YES" : "NO",
              currentFile_ ? (unsigned)currentFile_.size() : 0,
              currentFile_ ? (unsigned)currentFile_.position() : 0);
    currentFile_.flush();
    
    if (measuring_ && !cancelled_) {
        progressPercent_ = 100;
        LOG_INFO("Streaming complete. Mode=%s, Total rows: %d, File size: %u bytes", 
                 sweepVDS ? "VDS" : "VGS", rowCount, 
                 currentFile_ ? (unsigned)currentFile_.size() : 0);
    }
    
    // Shutdown DACs for safety
    hal::shutdown();
}

void MOSFETController::calculateCurveParams(CurveData& curve) {
    if(curve.ids.empty() || curve.vgs_true.empty()) return;

    // All calculations use the true terminal voltage vgs_true = vg_read - vsh
    // so that Gm, Vt, and SS reflect the actual MOSFET bias, not the commanded target.

    // 1. Calculate Gm using MathEngine (smooth derivative) — x-axis: vgs_true
    math_engine::GmConfig gmConfig;
    gmConfig.smoothingWindow = 5;
    gmConfig.useSavitzkyGolay = true;

    curve.gm = math_engine::calculateGm(curve.ids, curve.vgs_true, gmConfig);

    // 2. Calculate Vt using MathEngine (Peak Gm + Extrapolation) — x-axis: vgs_true
    curve.vt = math_engine::calculateVt(curve.gm, curve.vgs_true, curve.ids);

    // 3. Find Max Gm
    auto max_gm_it = std::max_element(curve.gm.begin(), curve.gm.end());
    curve.max_gm = (max_gm_it != curve.gm.end()) ? *max_gm_it : 0.0f;

    // 4. Calculate SS and Tangent Line — x-axis: vgs_true
    math_engine::SSResult ssResult = math_engine::calculateSS(curve.ids, curve.vgs_true);

    if (ssResult.valid) {
        curve.ss = ssResult.ss_mVdec;
        curve.ss_x1 = ssResult.x1;
        curve.ss_y1 = ssResult.y1;
        curve.ss_x2 = ssResult.x2;
        curve.ss_y2 = ssResult.y2;
    } else {
        curve.ss = 0.0f;
    }
}

void MOSFETController::writeEnhancedCSV(const std::vector<CurveData>& results) {
    if(results.empty()) {
        LOG_ERROR("writeEnhancedCSV: No data to write!");
        return;
    }
    
    // Debug FS info
    size_t totalBytes = FFat.totalBytes();
    size_t freeBytes = FFat.freeBytes();
    LOG_INFO("FFat Total: %u bytes, Free: %u bytes", (unsigned)totalBytes, (unsigned)freeBytes);
    
    if (totalBytes == 0) {
        LOG_ERROR("FFat partition not available or not formatted!");
        hasError_ = true;
        errorMessage_ = "FFat partition not available";
        return;
    }
    
    if (freeBytes < 10000) {
        LOG_ERROR("Not enough free space on FFat partition!");
        hasError_ = true;
        errorMessage_ = "Storage full - delete old files";
        return;
    }

    // Open file using the stored filename
    String path = String(FileManager::MEASUREMENTS_DIR) + "/" + currentFilename_;
    LOG_INFO("Opening file for write: %s", path.c_str());
    
    // Use local file object to avoid any member-variable state issues
    File file = FFat.open(path.c_str(), FILE_WRITE);
    if(!file) {
        LOG_ERROR("Failed to open file destination: %s", path.c_str());
        LOG_ERROR("FFat.open returned false - check partition table");
        return;
    }
    
    LOG_INFO("File opened successfully. Starting write...");
    
    // Use a buffer for writing - more efficient
    char lineBuf[256];
    size_t totalWritten = 0;
    
    // Write Header
    int len = snprintf(lineBuf, sizeof(lineBuf), "# MOSFET Characterization Data\n");
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    time_t now; time(&now);
    struct tm* timeinfo = localtime(&now);
    len = snprintf(lineBuf, sizeof(lineBuf), "# Date: %04d-%02d-%02d %02d:%02d:%02d\n",
        timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday,
        timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# Rshunt: %.2f Ohms\n", config_.rshunt);
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# VDS Range: %.2f to %.2f V (step %.2f)\n",
        config_.vds_start, config_.vds_end, config_.vds_step);
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# VGS Range: %.2f to %.2f V (step %.3f)\n",
        config_.vgs_start, config_.vgs_end, config_.vgs_step);
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "# Settling Time: %d ms\n", config_.settling_ms);
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    len = snprintf(lineBuf, sizeof(lineBuf), "#\n# Analysis Results (Per VDS Curve):\n");
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    // Write summary per curve
    for(const auto& res : results) {
        len = snprintf(lineBuf, sizeof(lineBuf), "# VDS=%.2fV Vt=%.3fV SS=%.2f mV/dec MaxGm=%.3e S\n", 
            res.vds, res.vt, res.ss, res.max_gm);
        totalWritten += file.write((uint8_t*)lineBuf, len);
    }
    
    len = snprintf(lineBuf, sizeof(lineBuf), "#\n");
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    // Column Headers — canonical order matching streaming path
    len = snprintf(lineBuf, sizeof(lineBuf), "timestamp,vds_sent,vgs_sent,vd_read,vg_read,vsh,vsh_precise,vds_true,vgs_true,ids\n");
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    LOG_INFO("Header written: %u bytes", (unsigned)totalWritten);
    
    int rowCount = 0;
    
    // Write Data - use snprintf for reliable formatting
    for(const auto& res : results) {
        for(size_t i=0; i<res.vgs.size(); i++) {
            len = snprintf(lineBuf, sizeof(lineBuf), "%lu,%.3f,%.3f,%.3f,%.3f,%.6f,%.6f,%.4f,%.4f,%.6e\n",
                (unsigned long)res.timestamps[i],
                res.vds,
                res.vgs[i],
                res.vd_read[i],
                res.vg_read[i],
                res.vsh[i],
                res.vsh_precise[i],
                res.vds_true[i],
                res.vgs_true[i],
                res.ids[i]
            );
            
            size_t written = file.write((uint8_t*)lineBuf, len);
            if(written != (size_t)len) {
                LOG_ERROR("Write mismatch at row %d: expected %d, got %u", rowCount, len, (unsigned)written);
            }
            totalWritten += written;
            
            // Feed watchdog during large writes
            if(rowCount % 100 == 0) {
                vTaskDelay(1);
            }
            rowCount++;
        }
    }
    
    LOG_INFO("Data write complete: %d rows, %u bytes", rowCount, (unsigned)totalWritten);
    
    // Flush and verify
    file.flush();
    
    size_t finalSize = file.size();
    LOG_INFO("File size after flush: %u bytes", (unsigned)finalSize);
    
    file.close();
    
    // Verify by reopening
    File verify = FFat.open(path.c_str(), FILE_READ);
    if (verify) {
        size_t verifySize = verify.size();
        verify.close();
        LOG_INFO("Verified file exists: %s (%u bytes)", currentFilename_.c_str(), (unsigned)verifySize);
    } else {
        LOG_ERROR("Verification failed - could not reopen file!");
    }
}
