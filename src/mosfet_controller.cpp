#include "mosfet_controller.h"
#include "hardware_hal.h"
#include "file_manager.h"
#include "led_status.h"
#include "math_engine.h"
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
        outer_steps = (int)((vgs_end - vgs_start) / vgs_step) + 1;
        inner_steps = (int)((vds_end - vds_start) / vds_step) + 1;
    } else {
        outer_steps = (int)((vds_end - vds_start) / vds_step) + 1;
        inner_steps = (int)((vgs_end - vgs_start) / vgs_step) + 1;
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
    
    // Column Headers
    len = snprintf(lineBuf, sizeof(lineBuf), "#\ntimestamp,vds,vgs,vsh,ids\n");
    currentFile_.write((uint8_t*)lineBuf, len);
    currentFile_.flush();
    
    LOG_INFO("Header written. Starting %s sweep...", sweepVDS ? "VDS" : "VGS");
    
    int rowCount = 0;
    
    // Temporary buffer for one curve (cleared after each outer loop iteration)
    CurveData currentCurve;
    
    if (sweepVDS) {
        // MODE: Curva Id x Vds (outer=VGS, inner=VDS)
        for (float vgs = vgs_start; vgs <= vgs_end && measuring_ && !cancelled_; vgs += vgs_step) {
            currentVds_ = vgs; // Use for progress display (outer loop var)
            
            for (float vds = vds_start; vds <= vds_end && measuring_ && !cancelled_; vds += vds_step) {
                hal::setVDS(vds);
                hal::setVGS(vgs);
                vTaskDelay(pdMS_TO_TICKS(settling));
                
                float vsh = readAnalogVoltage();
                float ids = vsh / rshunt;
                
                // Safe formatted write
                // %lu = unsigned long (timestamp)
                // %.3f = VDS, VGS (3 decimals)
                // %.6f = Vsh (6 decimals)
                // %.6e = Ids (scientific)
                currentFile_.printf("%lu,%.3f,%.3f,%.6f,%.6e\n", 
                           (unsigned long)millis(), vds, vgs, vsh, ids);
                
                rowCount++;
                current_point++;
                progressPercent_ = (current_point * 100) / total_points;
                
                if (rowCount % 50 == 0) {
                    currentFile_.flush();
                    vTaskDelay(1);
                }
            }
            
            // In VDS mode, parameters like Vt/SS/Gm are not strictly defined per VDS curve
            currentFile_.flush();
            LOG_INFO("VGS=%.3fV streamed. Rows: %d", vgs, rowCount);
        }
    } else {
        // MODE: Curva Id x Vgs (outer=VDS, inner=VGS) - default
        for (float vds = vds_start; vds <= vds_end && measuring_ && !cancelled_; vds += vds_step) {
            currentVds_ = vds;
            
            // Reset curve buffer for this VDS value
            currentCurve = CurveData();
            currentCurve.vds = vds;
            currentCurve.rshunt = rshunt; // Pass Rshunt for SS context if needed
            
            for (float vgs = vgs_start; vgs <= vgs_end && measuring_ && !cancelled_; vgs += vgs_step) {
                hal::setVDS(vds);
                hal::setVGS(vgs);
                vTaskDelay(pdMS_TO_TICKS(settling));
                
                float vsh = readAnalogVoltage();
                float ids = vsh / rshunt;
                
                // Buffer data for parameter calculation
                currentCurve.vgs.push_back(vgs);
                currentCurve.ids.push_back(ids);
                currentCurve.vsh.push_back(vsh);
                currentCurve.timestamps.push_back(millis());
                
                // Safe write
                currentFile_.printf("%lu,%.3f,%.3f,%.6f,%.6e\n", 
                           (unsigned long)millis(), vds, vgs, vsh, ids);
                
                rowCount++;
                current_point++;
                progressPercent_ = (current_point * 100) / total_points;
                
                if (rowCount % 50 == 0) {
                    currentFile_.flush();
                    vTaskDelay(1);
                }
            }
            
            // Calculate parameters for this curve
            calculateCurveParams(currentCurve);
            
            // Write curve metadata as comment using printf for safety
            currentFile_.printf("# VDS=%.3fV: Vt=%.3fV, SS=%.2f mV/dec, MaxGm=%.2e S, SS_Tangent_VGS:%.3f,%.3f SS_Tangent_LogId:%.3f,%.3f\n", 
                       vds, currentCurve.vt, currentCurve.ss, currentCurve.max_gm,
                       currentCurve.ss_x1, currentCurve.ss_x2, currentCurve.ss_y1, currentCurve.ss_y2);
            
            currentFile_.flush();
            LOG_INFO("VDS=%.3fV: Vt=%.3f, SS=%.1f mV/dec, MaxGm=%.2e", vds, currentCurve.vt, currentCurve.ss, currentCurve.max_gm);
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
    if(curve.ids.empty() || curve.vgs.empty()) return;
    
    // 1. Calculate Gm using MathEngine (smooth derivative)
    math_engine::GmConfig gmConfig;
    gmConfig.smoothingWindow = 5;
    gmConfig.useSavitzkyGolay = true;
    
    curve.gm = math_engine::calculateGm(curve.ids, curve.vgs, gmConfig);
    
    // 2. Calculate Vt using MathEngine (Peak Gm + Extrapolation)
    curve.vt = math_engine::calculateVt(curve.gm, curve.vgs, curve.ids);
    
    // 3. Find Max Gm
    auto max_gm_it = std::max_element(curve.gm.begin(), curve.gm.end());
    curve.max_gm = (max_gm_it != curve.gm.end()) ? *max_gm_it : 0.0f;
    
    // 4. Calculate SS and Tangent Line using MathEngine
    math_engine::SSResult ssResult = math_engine::calculateSS(curve.ids, curve.vgs);
    
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
    
    // Column Headers
    len = snprintf(lineBuf, sizeof(lineBuf), "timestamp,vds,vgs,vsh,ids,gm\n");
    totalWritten += file.write((uint8_t*)lineBuf, len);
    
    LOG_INFO("Header written: %u bytes", (unsigned)totalWritten);
    
    int rowCount = 0;
    
    // Write Data - use snprintf for reliable formatting
    for(const auto& res : results) {
        for(size_t i=0; i<res.vgs.size(); i++) {
            len = snprintf(lineBuf, sizeof(lineBuf), "%lu,%.3f,%.3f,%.4f,%.6e,%.6e\n",
                (unsigned long)res.timestamps[i],
                res.vds,
                res.vgs[i],
                res.vsh[i],
                res.ids[i],
                res.gm[i]
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
