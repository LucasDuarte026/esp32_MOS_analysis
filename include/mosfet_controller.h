#ifndef MOSFET_CONTROLLER_H
#define MOSFET_CONTROLLER_H

#include <Arduino.h>
#include <FFat.h>
#include <vector>
#include "log_buffer.h"

// Hardware pin definitions moved to hardware_hal.h

struct SweepConfig
{
    float vgs_start;
    float vgs_end;
    float vgs_step;
    float vds_start;
    float vds_end;
    float vds_step;
    float rshunt;
    int settling_ms;
    String filename; // Added filename to config
};

struct DataPoint
{
    uint32_t timestamp;    // Simple millis() timestamp
    float vds_target;      // Target VDS (outer loop)
    float vgs_target;      // Target VGS (inner loop)
    float vsh_measured;    // Measured voltage on shunt (from ADC GPIO34)
    
    // Calculated values
    float ids;  // vsh_measured / rshunt
    float gm;   // dIds/dVgs  
    float vt;   // threshold voltage
    float ss;   // subthreshold swing
};

class MOSFETController
{
public:
    MOSFETController();
    ~MOSFETController();

    void begin();
    
    // Measurement control
    bool startMeasurement(const SweepConfig& config);
    void stopMeasurement();
    void cancelMeasurement(); // NEW: Stops and deletes file
    bool isMeasuring() const;
    
    // Streaming implementation eliminates buffered getters
    // void getDataJSON() const; // REMOVED
    // void generateCSV() const; // REMOVED
    
    // Async control
    bool startMeasurementAsync(const SweepConfig& config);
    
    struct ProgressStatus {
        bool is_running;
        float current_vds;
        int progress_percent;
        String message;
        bool has_error;
        String error_message;
    };
    
    ProgressStatus getProgress() const;
    void reset();

private:
    void performSweep();
    static void measurementTaskWrapper(void* param);
    
    float readAnalogVoltage();
    bool openMeasurementFile();
    void closeMeasurementFile();

    SweepConfig config_;
    bool measuring_ = false;
    bool cancelled_ = false; // Track if cancelled
    SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
    
    struct CurveData {
        float vds;
        float vt;
        float ss; // mV/dec
        float max_gm;
        std::vector<float> vgs;
        std::vector<float> ids;
        std::vector<float> gm;
        std::vector<float> vsh;
        std::vector<uint32_t> timestamps;
    };
    
    // Analysis helpers
    void calculateCurveParams(CurveData& curve);
    void writeEnhancedCSV(const std::vector<CurveData>& results);

    // Buffering
    std::vector<CurveData> results_buffer_;

    // Progress tracking
    volatile float currentVds_ = 0.0f;
    volatile int progressPercent_ = 0;
    
    
    // Streaming file handle
    File currentFile_;
    String currentFilename_; // Store for deletion on cancel
    
    // Error tracking
    bool hasError_ = false;
    String errorMessage_;
};

#endif // MOSFET_CONTROLLER_H
