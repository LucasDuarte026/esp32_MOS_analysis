#include "mosfet_controller.h"
#include "hardware_hal.h"
#include "file_manager.h"
#include "led_status.h"
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
            currentFile_.printf("# VDS=%.3fV: Vt=%.3fV, SS=%.2f mV/dec, MaxGm=%.2e S\n", 
                       vds, currentCurve.vt, currentCurve.ss, currentCurve.max_gm);
            
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
    
    // 4. SS = 1/Slope of Log(Ids) vs Vgs - Using derivative-based detection
    // Find region where d(log10(Ids))/dVgs is consistent (exponential behavior)
    // This approach doesn't rely on hardcoded current values
    
    // Step 1: Calculate slopes at each point
    std::vector<float> slopes(n, 0.0f);
    std::vector<bool> valid(n, false);
    
    for(size_t i=1; i<n-1; i++) {
        float ids_prev = fabs(curve.ids[i-1]);
        float ids_next = fabs(curve.ids[i+1]);
        
        // Skip if current is too small (noise floor)
        if(ids_prev < 1e-12f || ids_next < 1e-12f) continue;
        
        float dVgs = curve.vgs[i+1] - curve.vgs[i-1];
        float dLogIds = log10(ids_next) - log10(ids_prev);
        
        // Positive slope means current is increasing (subthreshold region)
        if(fabs(dVgs) > 1e-9f && dLogIds > 0.01f) {
            slopes[i] = dLogIds / dVgs; // decades/V (inverse of SS)
            valid[i] = (slopes[i] > 0.5f); // At least 0.5 dec/V
        }
    }
    
    // Step 2: Find longest run of consistent slopes
    size_t bestStart = 0, bestLen = 0;
    size_t currStart = 0, currLen = 0;
    
    for(size_t i=1; i<n; i++) {
        if(!valid[i]) {
            if(currLen > bestLen) {
                bestStart = currStart;
                bestLen = currLen;
            }
            currLen = 0;
            currStart = i+1;
            continue;
        }
        
        if(currLen == 0 || !valid[i-1]) {
            currStart = i;
            currLen = 1;
        } else {
            float ratio = slopes[i] / slopes[i-1];
            // Check if slopes are consistent (within 2x of each other)
            if(ratio > 0.5f && ratio < 2.0f) {
                currLen++;
            } else {
                if(currLen > bestLen) {
                    bestStart = currStart;
                    bestLen = currLen;
                }
                currStart = i;
                currLen = 1;
            }
        }
    }
    if(currLen > bestLen) {
        bestStart = currStart;
        bestLen = currLen;
    }
    
    // Step 3: Calculate SS from the best run using regression
    if(bestLen >= 3) {
        std::vector<float> x, y;
        for(size_t i=bestStart; i<bestStart+bestLen && i<n; i++) {
            float val = fabs(curve.ids[i]);
            if(val > 1e-15f) {
                x.push_back(curve.vgs[i]);
                y.push_back(log10(val));
            }
        }
        
        if(x.size() > 3) {
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
                    float ss_val = (float)((1.0/fabs(slope)) * 1000.0);
                    if(ss_val > 0 && ss_val < 2000) { // Valid range
                        curve.ss = ss_val;
                    }
                }
            }
        }
    }
    // Note: SS remains 0.0 if not calculable (e.g., no exponential region found)
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
