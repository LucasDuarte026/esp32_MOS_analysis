#include "mosfet_controller.h"
#include "file_manager.h"
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
    
    analogReadResolution(ADC_RESOLUTION);
    analogSetAttenuation(ADC_11db);
    pinMode(ADC_PIN, INPUT);
    
    LOG_INFO("MOSFET Controller initialized - ADC on GPIO%d", ADC_PIN);
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
    if (currentFile_) {
        currentFile_.close();
        LOG_INFO("File closed successfully");
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
    int rawValue = analogRead(ADC_PIN);
    return (rawValue / (float)(1 << ADC_RESOLUTION)) * ADC_VREF;
}

void MOSFETController::performSweep()
{
    results_buffer_.clear();
    
    const float vds_start = config_.vds_start;
    const float vds_end = config_.vds_end;
    const float vds_step = config_.vds_step;
    const float vgs_start = config_.vgs_start;
    const float vgs_end = config_.vgs_end;
    const float vgs_step = config_.vgs_step;
    const float rshunt = config_.rshunt;
    const int settling = config_.settling_ms;
    
    // Calculate total steps for progress
    int vds_steps = (int)((vds_end - vds_start) / vds_step) + 1;
    int vgs_steps_per_curve = (int)((vgs_end - vgs_start) / vgs_step) + 1;
    int total_points = vds_steps * vgs_steps_per_curve;
    int current_point = 0;
    
    // Outer loop: VDS sweep
    for (float vds = vds_start; vds <= vds_end && measuring_ && !cancelled_; vds += vds_step) {
        currentVds_ = vds;
        
        CurveData curve;
        curve.vds = vds;
        
        // Inner loop: VGS sweep
        int point_count = 0;
        for (float vgs = vgs_start; vgs <= vgs_end && measuring_ && !cancelled_; vgs += vgs_step) {
            // Target voltage (in real hardware, apply this!)
            // dac.setVoltage(vds); dac.setVoltage(vgs);
            vTaskDelay(pdMS_TO_TICKS(settling));
            
            // Read shunt voltage
            float vsh = readAnalogVoltage();
            float ids = vsh / rshunt;
            
            curve.timestamps.push_back(millis());
            curve.vgs.push_back(vgs);
            curve.vsh.push_back(vsh);
            curve.ids.push_back(ids);
            
            point_count++;
            current_point++;
            progressPercent_ = (current_point * 100) / total_points;
            
            // Feed watchdog
            if (point_count % 10 == 0) vTaskDelay(1);
        }
        
        // Analyze this curve
        calculateCurveParams(curve);
        results_buffer_.push_back(curve);
        
        LOG_INFO("VDS=%.2fV processed. Points: %d", vds, (int)curve.vgs.size());
    }
    
    // Sweep done. Write file.
    if (measuring_ && !cancelled_) {
        writeEnhancedCSV(results_buffer_);
        progressPercent_ = 100;
        LOG_INFO("Sweep complete. Total curves: %d", (int)results_buffer_.size());
    }
}

void MOSFETController::calculateCurveParams(CurveData& curve) {
    if (curve.vgs.size() < 5) return;
    
    size_t n = curve.vgs.size();
    
    // 1. Smooth IDs (Moving Average window=5)
    std::vector<float> ids_smooth(n);
    for(size_t i=0; i<n; i++) {
        float sum = 0;
        int c = 0;
        for(int k=-2; k<=2; k++) {
            if((int)i+k >= 0 && (int)i+k < (int)n) {
                sum += curve.ids[i+k];
                c++;
            }
        }
        ids_smooth[i] = sum/c;
    }
    
    // 2. Gm = dIds/dVgs (Central Difference)
    curve.gm.assign(n, 0.0f);
    for(size_t i=1; i<n-1; i++) {
        float dv = curve.vgs[i+1] - curve.vgs[i-1];
        if(fabs(dv) > 1e-9) {
            curve.gm[i] = (ids_smooth[i+1] - ids_smooth[i-1]) / dv;
        }
    }
    
    // 3. Vt = Peak of 2nd Derivative
    std::vector<float> d2_ids(n, 0.0f);
    for(size_t i=2; i<n-2; i++) {
        float dv = curve.vgs[i+1] - curve.vgs[i-1];
        if(fabs(dv) > 1e-9) {
            d2_ids[i] = (curve.gm[i+1] - curve.gm[i-1]) / dv;
        }
    }
    
    auto max_d2 = std::max_element(d2_ids.begin(), d2_ids.end());
    size_t idx_vt = std::distance(d2_ids.begin(), max_d2);
    curve.vt = curve.vgs[idx_vt];
    
    // Max Gm
    auto max_gm_it = std::max_element(curve.gm.begin(), curve.gm.end());
    curve.max_gm = *max_gm_it;
    
    // 4. SS = 1/Slope of Log(Ids) vs Vgs
    // Region: 1e-10 < Ids < 1e-6
    std::vector<float> x, y;
    for(size_t i=0; i<n; i++) {
        float val = fabs(curve.ids[i]);
        if(val > 1e-10 && val < 1e-6) {
            x.push_back(curve.vgs[i]);
            y.push_back(log10(val));
        }
    }
    
    if(x.size() > 5) {
        // Linear regression
        double sx=0, sy=0, sxy=0, sxx=0;
        size_t N = x.size();
        for(size_t i=0; i<N; i++) {
            sx += x[i]; sy += y[i];
            sxy += x[i]*y[i]; sxx += x[i]*x[i];
        }
        double denominator = N*sxx - sx*sx;
        if(fabs(denominator) > 1e-9) {
            double slope = (N*sxy - sx*sy) / denominator;
            if(fabs(slope) > 1e-9) {
                // SS = (1/slope) * 1000 mV/dec
                curve.ss = (float)((1.0/fabs(slope)) * 1000.0);
            }
        }
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
