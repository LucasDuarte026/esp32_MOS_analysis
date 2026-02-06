#ifndef MATH_ENGINE_H
#define MATH_ENGINE_H

#include <Arduino.h>
#include <vector>

// ============================================================================
// Math Engine for MOSFET Parameter Calculation
// ============================================================================
// Provides algorithms for calculating key MOSFET parameters from IV curves:
//   - Gm (Transconductance): dIds/dVgs
//   - Vt (Threshold Voltage): Extrapolated from max Gm point
//   - SS (Subthreshold Swing): mV/decade in exponential region
// ============================================================================

namespace math_engine {

/**
 * @brief Result of SS (Subthreshold Swing) calculation
 * 
 * Contains the SS value in mV/decade plus coordinates for drawing
 * the tangent line on a log-scale graph.
 */
struct SSResult {
    float ss_mVdec = 0.0f;      // SS in mV/decade
    bool valid = false;          // True if calculation succeeded
    
    // Tangent line coordinates (for visualization)
    // Line from (x1, y1) to (x2, y2) on VGS vs log10(IDS) plot
    float x1 = 0.0f;  // VGS start
    float y1 = 0.0f;  // log10(IDS) start
    float x2 = 0.0f;  // VGS end
    float y2 = 0.0f;  // log10(IDS) end
    
    // Region used for calculation
    size_t regionStart = 0;
    size_t regionEnd = 0;
};

/**
 * @brief Configuration for Gm calculation
 */
struct GmConfig {
    size_t smoothingWindow = 5;  // Moving average window size
    bool useSavitzkyGolay = true; // Use Savitzky-Golay filter for better SNR
};

/**
 * @brief Calculate transconductance (Gm = dIds/dVgs)
 * 
 * Uses central difference with optional smoothing.
 * 
 * @param ids Vector of drain current values
 * @param vgs Vector of gate-source voltage values
 * @param config Optional configuration for smoothing
 * @return Vector of Gm values (same size as input)
 */
std::vector<float> calculateGm(
    const std::vector<float>& ids, 
    const std::vector<float>& vgs,
    const GmConfig& config = GmConfig()
);

/**
 * @brief Find threshold voltage (Vt) using maximum Gm extrapolation
 * 
 * Method:
 * 1. Find point of maximum Gm
 * 2. Draw tangent line at that point
 * 3. Vt = x-intercept of tangent line
 * 
 * @param gm Vector of transconductance values
 * @param vgs Vector of gate-source voltage values
 * @param ids Vector of drain current values
 * @return Vt in Volts, or 0 if calculation fails
 */
float calculateVt(
    const std::vector<float>& gm, 
    const std::vector<float>& vgs,
    const std::vector<float>& ids
);

/**
 * @brief Calculate Subthreshold Swing (SS)
 * 
 * Automatically detects the subthreshold region (exponential behavior)
 * and performs linear regression on log10(Ids) vs Vgs.
 * 
 * SS = (1/slope) * 1000 mV/decade
 * 
 * Also returns tangent line coordinates for visualization.
 * 
 * @param ids Vector of drain current values
 * @param vgs Vector of gate-source voltage values
 * @return SSResult with SS value and tangent coordinates
 */
SSResult calculateSS(
    const std::vector<float>& ids, 
    const std::vector<float>& vgs
);

/**
 * @brief Apply Savitzky-Golay smoothing filter
 * 
 * @param data Input data vector
 * @param windowSize Window size (must be odd, default 5)
 * @param polyOrder Polynomial order (default 2)
 * @return Smoothed data vector
 */
std::vector<float> savitzkyGolaySmooth(
    const std::vector<float>& data,
    size_t windowSize = 5,
    size_t polyOrder = 2
);

/**
 * @brief Simple moving average smoothing
 * 
 * @param data Input data vector
 * @param windowSize Window size for averaging
 * @return Smoothed data vector
 */
std::vector<float> movingAverageSmooth(
    const std::vector<float>& data,
    size_t windowSize = 5
);

/**
 * @brief Linear regression on (x, y) data
 * 
 * @param x X-axis data
 * @param y Y-axis data
 * @param slope Output: slope of regression line
 * @param intercept Output: y-intercept of regression line
 * @return R² value (0-1, higher is better fit)
 */
float linearRegression(
    const std::vector<float>& x,
    const std::vector<float>& y,
    float& slope,
    float& intercept
);

} // namespace math_engine

#endif // MATH_ENGINE_H
