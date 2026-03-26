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
    LOG_INFO("  VDS: %.2fV-%.2fV | VGS: %.2fV-%.2fV", 
             config.vds_start, config.vds_end, 
             config.vgs_start, config.vgs_end);
    
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
// Strategy ("smart-fast"):
//   1. Set the DAC to the raw target and wait for the rail to settle.
//   2. Do ONE fast ADC read-back.  If |error| <= threshold → done (cheap path).
//   3. Only if the error exceeds the threshold enter the iterative correction
//      loop (max DAC_CALIB_MAX_ITER iterations), nudging the probe by the
//      measured error each time.
//   4. If the loop exhausts without meeting the target, emit a WARN and
//      continue with the best estimate so the sweep is not blocked.
//
// This means the "stable rail" cost is exactly ONE settling delay + ONE ADC
// read per calibration call — negligible overhead.

float MOSFETController::calibrateVD(float target_vd, int settling_ms)
{
    float probe = target_vd;
    hal::setVDS(probe);
    vTaskDelay(pdMS_TO_TICKS(settling_ms));
    float read_back = hal::readVD_Actual();
    float error     = target_vd - read_back;

    // Fast path: already within tolerance
    if (fabsf(error) <= VD_GLOBAL_ERROR) {
        return probe;
    }

    // Slow path: iterative correction
    for (int iter = 1; iter < DAC_CALIB_MAX_ITER; iter++) {
        probe += error;  // nudge toward target
        // Clamp to avoid sending the DAC out of its valid range
        if (probe < 0.0f) probe = 0.0f;
        if (probe > 5.5f) probe = 5.5f;

        hal::setVDS(probe);
        vTaskDelay(pdMS_TO_TICKS(settling_ms));
        read_back = hal::readVD_Actual();
        error     = target_vd - read_back;

        if (fabsf(error) <= VD_GLOBAL_ERROR) {
            LOG_DEBUG("[CALIB VD] target=%.3f converged in %d iter(s), probe=%.3f read=%.3f err=%.4f",
                      target_vd, iter + 1, probe, read_back, error);
            return probe;
        }
    }

    // Give up — emit WARN, keep best estimate
    float vsh = hal::readShuntVoltage();
    LOG_WARN("[CALIB VD] FAILED to meet tolerance after %d iters "
             "| target=%.3fV probe_sent=%.3fV vd_read=%.3fV err=%.4fV (threshold=%.3fV) vsh=%.3fV",
             DAC_CALIB_MAX_ITER, target_vd, probe, read_back, error, VD_GLOBAL_ERROR, vsh);
    return probe;
}

float MOSFETController::calibrateVG(float target_vg, int settling_ms)
{
    float probe = target_vg;
    hal::setVGS(probe);
    vTaskDelay(pdMS_TO_TICKS(settling_ms));
    float read_back = hal::readVG_Actual();
    float error     = target_vg - read_back;

    // Fast path: already within tolerance
    if (fabsf(error) <= VG_GLOBAL_ERROR) {
        return probe;
    }

    // Slow path: iterative correction
    for (int iter = 1; iter < DAC_CALIB_MAX_ITER; iter++) {
        probe += error;
        if (probe < 0.0f) probe = 0.0f;
        if (probe > 5.5f) probe = 5.5f;

        hal::setVGS(probe);
        vTaskDelay(pdMS_TO_TICKS(settling_ms));
        read_back = hal::readVG_Actual();
        error     = target_vg - read_back;

        if (fabsf(error) <= VG_GLOBAL_ERROR) {
            LOG_DEBUG("[CALIB VG] target=%.3f converged in %d iter(s), probe=%.3f read=%.3f err=%.4f",
                      target_vg, iter + 1, probe, read_back, error);
            return probe;
        }
    }

    float vsh = hal::readShuntVoltage();
    LOG_WARN("[CALIB VG] FAILED to meet tolerance after %d iters "
             "| target=%.3fV probe_sent=%.3fV vg_read=%.3fV err=%.4fV (threshold=%.3fV) vsh=%.3fV",
             DAC_CALIB_MAX_ITER, target_vg, probe, read_back, error, VG_GLOBAL_ERROR, vsh);
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

    // ADC gain metadata
    const char* gainLabel;
    switch (config_.adc_gain) {
        case  0: gainLabel = "GAIN_TWOTHIRDS (±6.144 V)"; break;
        case  1: gainLabel = "GAIN_ONE (±4.096 V)";       break;
        case  2: gainLabel = "GAIN_TWO (±2.048 V)";       break;
        case  4: gainLabel = "GAIN_FOUR (±1.024 V)";      break;
        case  8: gainLabel = "GAIN_EIGHT (±0.512 V)";     break;
        case 16: gainLabel = "GAIN_SIXTEEN (±0.256 V)";   break;
        default: gainLabel = "GAIN_SIXTEEN (±0.256 V)";   break;
    }
    len = snprintf(lineBuf, sizeof(lineBuf), "# ADC Gain: %s\n", gainLabel);
    currentFile_.write((uint8_t*)lineBuf, len);

    // Hardware mode metadata — records which peripherals collected the data
    if (config_.use_external_hw) {
        len = snprintf(lineBuf, sizeof(lineBuf),
            "# Hardware: Fully External (VDS: MCP4725 0x61 12-bit, VGS: MCP4725 0x60 12-bit, ADC: ADS1115 0x48 16-bit)\n");
    } else {
        len = snprintf(lineBuf, sizeof(lineBuf),
            "# Hardware: ESP32 Internal (VDS: DAC 8-bit, VGS: DAC 8-bit, ADC: 12-bit)\n");
    }
    currentFile_.write((uint8_t*)lineBuf, len);

    len = snprintf(lineBuf, sizeof(lineBuf), "# Firmware: %s\n", SOFTWARE_VERSION);
    currentFile_.write((uint8_t*)lineBuf, len);
    
    // Configure ADC gain for shunt measurement
    if (config_.use_external_hw) {
        hal::setADC_Gain(config_.adc_gain);
    }

    // Column Headers — new canonical order
    len = snprintf(lineBuf, sizeof(lineBuf), "#\ntimestamp,vd,vg,vd_read,vg_read,vsh,vds_true,vgs_true,ids\n");
    currentFile_.write((uint8_t*)lineBuf, len);
    currentFile_.flush();
    
    LOG_INFO("Starting %s sweep - Oversampling: %s (%dx), Settling: %dms, GainCode: %d", 
        sweepVDS ? "VD" : "VG",
        config_.oversampling > 1 ? "ON" : "OFF", 
        config_.oversampling, 
        config_.settling_ms,
        config_.adc_gain);
    
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

            // Calibrate VGS once at the start of this curve
            float probe_vg = calibrateVG(vgs, settling);

            for (int i_vds = 0; i_vds < inner_steps && measuring_ && !cancelled_; i_vds++) {
                float vds = vds_start + i_vds * vds_step;

                // VDS changes every step → full calibration every time
                float probe_vd = calibrateVD(vds, settling);
                (void)probe_vd; // result used for DAC side-effect; final value not needed

                // Smart-fast VG verify: one read, re-calibrate only if drifted
                {
                    float vg_now = hal::readVG_Actual();
                    if (fabsf(vg_now - vgs) > VG_GLOBAL_ERROR) {
                        probe_vg = calibrateVG(vgs, settling);
                    }
                }

                float vsh       = hal::readShuntVoltage();
                float ids       = vsh / rshunt;
                float vd_actual = hal::readVD_Actual();
                float vg_actual = hal::readVG_Actual();
                float vds_true  = vd_actual - vsh;
                float vgs_true  = vg_actual - vsh;

                currentFile_.printf("%lu,%.3f,%.3f,%.3f,%.3f,%.6f,%.4f,%.4f,%.6e\n",
                           (unsigned long)millis(), vds, vgs,
                           vd_actual, vg_actual, vsh,
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
            LOG_INFO("VGS=%.3fV streamed. Rows: %d", vgs, rowCount);
            (void)probe_vg; // used inside inner-loop re-calibration; suppress unused warning
        }

    } else {
        // Mode: Id vs Vgs sweep (outer = VDS fixed, inner = VGS swept) — default
        // −−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−−
        // Calibration strategy for IdVgs:
        //   - VDS does NOT change each inner step → calibrate once per outer
        //     curve, then do a fast verify-only check at every inner step.
        //   - VGS sweeps every inner step → always calibrate fully before each read.
        for (int i_vds = 0; i_vds < outer_steps && measuring_ && !cancelled_; i_vds++) {
            float vds = vds_start + i_vds * vds_step;
            currentVds_ = vds;

            // Reset curve buffer for this VDS value
            currentCurve = CurveData();
            currentCurve.vds = vds;
            currentCurve.rshunt = rshunt;

            // Calibrate VDS once for this curve; the drain rail should be stable
            float probe_vd = calibrateVD(vds, settling * 3);

            for (int i_vgs = 0; i_vgs < inner_steps && measuring_ && !cancelled_; i_vgs++) {
                float vgs = vgs_start + i_vgs * vgs_step;

                // VGS changes every step → full calibration every time
                float probe_vg = calibrateVG(vgs, settling);
                (void)probe_vg; // result used for DAC side-effect; final value not needed

                // Smart-fast VD verify: one read, re-calibrate only if drifted
                {
                    float vd_now = hal::readVD_Actual();
                    if (fabsf(vd_now - vds) > VD_GLOBAL_ERROR) {
                        probe_vd = calibrateVD(vds, settling);
                    }
                }

                uint32_t t_dac  = millis();
                uint32_t t_set  = millis();
                if (settling > 0) vTaskDelay(pdMS_TO_TICKS(settling));
                uint32_t t_adc  = millis();
                float vsh       = hal::readShuntVoltage();
                uint32_t t_done = millis();
                float ids       = vsh / rshunt;
                float vd_actual = hal::readVD_Actual();
                float vg_actual = hal::readVG_Actual();
                float vds_true_val = vd_actual - vsh;
                float vgs_true_val = vg_actual - vsh;

                // Buffer data for parameter calculation
                currentCurve.vgs.push_back(vgs);
                currentCurve.ids.push_back(ids);
                currentCurve.vsh.push_back(vsh);
                currentCurve.vd_read.push_back(vd_actual);
                currentCurve.vg_read.push_back(vg_actual);
                currentCurve.vds_true.push_back(vds_true_val);
                currentCurve.vgs_true.push_back(vgs_true_val);
                currentCurve.timestamps.push_back(millis());

                currentFile_.printf("%lu,%.3f,%.3f,%.3f,%.3f,%.6f,%.4f,%.4f,%.6e\n",
                           (unsigned long)millis(), vds, vgs,
                           vd_actual, vg_actual, vsh,
                           vds_true_val, vgs_true_val, ids);

                rowCount++;
                current_point++;
                progressPercent_ = (current_point * 100) / total_points;

                if (rowCount % 50 == 1) {
                    LOG_DEBUG("[TIMING] VGS=%.3fV | DAC write=%lums | Settle=%lums | ADC read=%lums | Total=%lums",
                              vgs,
                              (unsigned long)(t_set  - t_dac),
                              (unsigned long)(t_adc  - t_set),
                              (unsigned long)(t_done - t_adc),
                              (unsigned long)(t_done - t_dac));
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
            LOG_INFO("VDS=%.3fV: Vt=%.3f, SS=%.1f mV/dec, MaxGm=%.2e", vds, currentCurve.vt, currentCurve.ss, currentCurve.max_gm);
            (void)probe_vd; // used inside inner-loop re-calibration; suppress unused warning
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
    len = snprintf(lineBuf, sizeof(lineBuf), "timestamp,vd,vg,vd_read,vg_read,vsh,vds_true,vgs_true,ids\n");
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    LOG_INFO("Header written: %u bytes", (unsigned)totalWritten);
    
    int rowCount = 0;
    
    // Write Data - use snprintf for reliable formatting
    for(const auto& res : results) {
        for(size_t i=0; i<res.vgs.size(); i++) {
            len = snprintf(lineBuf, sizeof(lineBuf), "%lu,%.3f,%.3f,%.3f,%.3f,%.6f,%.4f,%.4f,%.6e\n",
                (unsigned long)res.timestamps[i],
                res.vds,
                res.vgs[i],
                res.vd_read[i],
                res.vg_read[i],
                res.vsh[i],
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
