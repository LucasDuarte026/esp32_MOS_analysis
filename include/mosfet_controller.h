#ifndef MOSFET_CONTROLLER_H
#define MOSFET_CONTROLLER_H

// ============================================================================
// MOSFET Controller — measurement sweep sequencer
// ============================================================================
// Drives VDS and VGS through a configurable grid, reads the shunt ADC, and
// streams each data point directly to FFat in a CSV file. The sweep runs on
// a dedicated FreeRTOS task (Core 1) so it never blocks the HTTP server.
//
// Typical usage:
//   1. Call begin() once from setup().
//   2. Build a SweepConfig and call startMeasurementAsync().
//   3. Poll getProgress() via /api/progress until is_running == false.
// ============================================================================

#include <Arduino.h>
#include <FFat.h>
#include <vector>
#include "log_buffer.h"

// Pin and HAL definitions live in hardware_hal.h.

// ----------------------------------------------------------------------------
// SweepMode — which axis is the inner (fast) loop
// ----------------------------------------------------------------------------
enum SweepMode {
    SWEEP_VGS,  ///< Id vs Vgs (transfer curve) — VDS fixed per curve, VGS swept
    SWEEP_VDS   ///< Id vs Vds (output curve)  — VGS fixed per curve, VDS swept
};

// ----------------------------------------------------------------------------
// SweepConfig — parameters for a single measurement run
// ----------------------------------------------------------------------------
struct SweepConfig {
    float vgs_start;            ///< Gate sweep start voltage (V)
    float vgs_end;              ///< Gate sweep end voltage (V), up to 5.0 V
    float vgs_step;             ///< Gate voltage increment per step (V)
    float vds_start;            ///< Drain sweep start voltage (V)
    float vds_end;              ///< Drain sweep end voltage (V)
    float vds_step;             ///< Drain voltage increment per step (V)
    float rshunt;               ///< Shunt resistor (Ω); Ids = Vsh / Rshunt
    int   settling_ms;          ///< Wait after setting a new voltage before sampling (ms)
    uint16_t oversampling = 16; ///< ADC samples averaged per point (1 = off, 16 = default)
    bool use_external_hw  = true; ///< true = MCP4725 + ADS1115; false = internal ESP32 peripherals
    String filename;            ///< Base filename (timestamp will be appended)
    SweepMode sweep_mode = SWEEP_VGS; ///< Which axis drives the inner loop
};

// ----------------------------------------------------------------------------
// DataPoint — a single acquired sample (used internally and for JSON export)
// ----------------------------------------------------------------------------
struct DataPoint {
    uint32_t timestamp;     ///< Time relative to boot when the ADC sample was taken (ms)
    float vds_target;       ///< Commanded VDS for this point (V)
    float vgs_target;       ///< Commanded VGS for this point (V)
    float vsh_measured;     ///< Voltage measured across the shunt resistor (V)

    // Derived values computed from per-curve analysis
    float ids;  ///< Drain current: Vsh / Rshunt (A)
    float gm;   ///< Transconductance: dIds/dVgs at this point (S)
    float vt;   ///< Threshold voltage extrapolated from peak Gm (V)
    float ss;   ///< Subthreshold swing (mV/decade)
};

// ============================================================================
// MOSFETController
// ============================================================================
class MOSFETController {
public:
    MOSFETController();
    ~MOSFETController();

    /** Initialise hardware (HAL) and create the FreeRTOS mutex. Call once from setup(). */
    void begin();

    // ---- Measurement control -----------------------------------------------

    /**
     * @brief Launch a sweep in a background FreeRTOS task.
     *
     * Returns immediately. If a sweep is already running this call fails.
     * Equivalent to startMeasurementAsync(); kept for API compatibility.
     */
    bool startMeasurement(const SweepConfig& config);

    /** Stop the running sweep and delete the partial output file. */
    void stopMeasurement();

    /** Cancel the running sweep and remove the incomplete CSV from storage. */
    void cancelMeasurement();

    /** Returns true while a sweep task is active. */
    bool isMeasuring() const;

    // ---- Async control -----------------------------------------------------

    /**
     * @brief Launch a sweep in a background FreeRTOS task.
     *
     * Returns immediately. The sweep runs on Core 1. Progress is readable
     * via getProgress(). Returns false if a sweep is already running or if
     * the FreeRTOS task cannot be created.
     *
     * @param config  Fully-populated sweep parameters.
     * @return true on success, false otherwise.
     */
    bool startMeasurementAsync(const SweepConfig& config);

    // ---- Progress ----------------------------------------------------------

    /** Snapshot of the current sweep state, safe to call from any task. */
    struct ProgressStatus {
        bool   is_running;        ///< true while the sweep task is alive
        float  current_vds;       ///< Outer-loop voltage being swept right now (V)
        int    progress_percent;  ///< 0–100
        String message;           ///< Human-readable status line
        bool   has_error;         ///< true if the sweep encountered a fatal error
        String error_message;     ///< Non-empty when has_error is true
    };

    /** Returns a copy of the current progress state. Thread-safe. */
    ProgressStatus getProgress() const;

    /** Cancel any running sweep and clear internal buffers. */
    void reset();

private:
    void   performSweep();
    static void measurementTaskWrapper(void* param);

    float readAnalogVoltage();
    bool  openMeasurementFile();
    void  closeMeasurementFile();

    SweepConfig      config_;
    bool             measuring_  = false;
    bool             cancelled_  = false;
    SemaphoreHandle_t mutex_     = nullptr;
    TaskHandle_t     taskHandle_ = nullptr;

    // Per-VDS curve data collected during a SWEEP_VGS pass.
    // Flushed to disk after calculateCurveParams() and then cleared.
    struct CurveData {
        float vds;      ///< Fixed VDS for this curve (V)
        float vt;       ///< Threshold voltage (V)
        float ss;       ///< Subthreshold swing (mV/decade)
        float max_gm;   ///< Peak transconductance (S)
        float rshunt;   ///< Rshunt passed in for downstream validity checks

        std::vector<float>    vgs;
        std::vector<float>    ids;
        std::vector<float>    gm;
        std::vector<float>    vsh;
        std::vector<uint32_t> timestamps;

        // Tangent line endpoints in (VGS, log10(Ids)) space for dashboard overlay
        float ss_x1 = 0, ss_y1 = 0;
        float ss_x2 = 0, ss_y2 = 0;
    };

    void calculateCurveParams(CurveData& curve);
    void writeEnhancedCSV(const std::vector<CurveData>& results);

    std::vector<CurveData> results_buffer_;

    volatile float currentVds_      = 0.0f;
    volatile int   progressPercent_ = 0;

    File   currentFile_;
    String currentFilename_;  ///< Stored so cancelMeasurement() can delete the partial file

    bool   hasError_     = false;
    String errorMessage_;
};

#endif // MOSFET_CONTROLLER_H
