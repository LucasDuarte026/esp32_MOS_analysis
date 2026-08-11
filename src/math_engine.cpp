#include "math_engine.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace math_engine {

// ============================================================================
// Smoothing Functions
// ============================================================================

std::vector<float> movingAverageSmooth(const std::vector<float>& data, size_t windowSize) {
    if (data.empty()) return {};
    if (windowSize < 1) windowSize = 1;
    if (windowSize % 2 == 0) windowSize++;  // Ensure odd window
    
    size_t n = data.size();
    std::vector<float> result(n);
    int halfWindow = windowSize / 2;
    
    for (size_t i = 0; i < n; i++) {
        float sum = 0;
        int count = 0;
        
        for (int k = -halfWindow; k <= halfWindow; k++) {
            int idx = static_cast<int>(i) + k;
            if (idx >= 0 && idx < static_cast<int>(n)) {
                sum += data[idx];
                count++;
            }
        }
        result[i] = sum / count;
    }
    
    return result;
}

std::vector<float> savitzkyGolaySmooth(const std::vector<float>& data, size_t windowSize, size_t polyOrder) {
    // Simplified Savitzky-Golay using pre-calculated coefficients for window=5, order=2
    // For ESP32 embedded use, we use a simpler moving average variant
    // Full S-G would require matrix inversion which is expensive
    
    if (data.size() < 5) return movingAverageSmooth(data, windowSize);
    
    // Coefficients for window=5, polynomial order=2 (normalized)
    // H = [-3, 12, 17, 12, -3] / 35
    const float c[] = {-3.0f/35.0f, 12.0f/35.0f, 17.0f/35.0f, 12.0f/35.0f, -3.0f/35.0f};
    
    size_t n = data.size();
    std::vector<float> result(n);
    
    // Apply convolution with boundary handling
    for (size_t i = 0; i < n; i++) {
        float sum = 0;
        for (int k = -2; k <= 2; k++) {
            int idx = static_cast<int>(i) + k;
            // Clamp to valid indices
            if (idx < 0) idx = 0;
            if (idx >= static_cast<int>(n)) idx = n - 1;
            sum += data[idx] * c[k + 2];
        }
        result[i] = sum;
    }
    
    return result;
}

// ============================================================================
// Linear Regression
// ============================================================================

float linearRegression(
    const std::vector<float>& x,
    const std::vector<float>& y,
    float& slope,
    float& intercept
) {
    if (x.size() != y.size() || x.size() < 2) {
        slope = 0;
        intercept = 0;
        return 0;
    }
    
    size_t n = x.size();
    
    // Calculate sums
    double sx = 0, sy = 0, sxy = 0, sxx = 0, syy = 0;
    for (size_t i = 0; i < n; i++) {
        sx += x[i];
        sy += y[i];
        sxy += x[i] * y[i];
        sxx += x[i] * x[i];
        syy += y[i] * y[i];
    }
    
    double denominator = n * sxx - sx * sx;
    if (fabs(denominator) < 1e-12) {
        slope = 0;
        intercept = static_cast<float>(sy / n);
        return 0;
    }
    
    slope = static_cast<float>((n * sxy - sx * sy) / denominator);
    intercept = static_cast<float>((sy - slope * sx) / n);
    
    // Calculate R²
    double ssRes = 0, ssTot = 0;
    double yMean = sy / n;
    for (size_t i = 0; i < n; i++) {
        double yPred = slope * x[i] + intercept;
        ssRes += (y[i] - yPred) * (y[i] - yPred);
        ssTot += (y[i] - yMean) * (y[i] - yMean);
    }
    
    if (ssTot < 1e-12) return 1.0f;
    return static_cast<float>(1.0 - ssRes / ssTot);
}

// ============================================================================
// Gm Calculation
// ============================================================================

std::vector<float> calculateGm(
    const std::vector<float>& ids, 
    const std::vector<float>& vgs,
    const GmConfig& config
) {
    if (ids.size() != vgs.size() || ids.size() < 3) {
        return std::vector<float>(ids.size(), 0.0f);
    }
    
    size_t n = ids.size();
    
    // Step 1: Smooth IDs data
    std::vector<float> idsSmooth;
    if (config.useSavitzkyGolay) {
        idsSmooth = savitzkyGolaySmooth(ids, config.smoothingWindow, 2);
    } else {
        idsSmooth = movingAverageSmooth(ids, config.smoothingWindow);
    }
    
    // Step 2: Calculate Gm using central difference
    std::vector<float> gm(n, 0.0f);
    
    for (size_t i = 1; i < n - 1; i++) {
        float dVgs = vgs[i + 1] - vgs[i - 1];
        if (fabs(dVgs) > 1e-9f) {
            gm[i] = (idsSmooth[i + 1] - idsSmooth[i - 1]) / dVgs;
        }
    }
    
    // Handle boundaries
    if (n >= 2) {
        float dVgs = vgs[1] - vgs[0];
        if (fabs(dVgs) > 1e-9f) {
            gm[0] = (idsSmooth[1] - idsSmooth[0]) / dVgs;
        }
        dVgs = vgs[n-1] - vgs[n-2];
        if (fabs(dVgs) > 1e-9f) {
            gm[n-1] = (idsSmooth[n-1] - idsSmooth[n-2]) / dVgs;
        }
    }
    
    return gm;
}

// ============================================================================
// Vt Calculation
// ============================================================================

float calculateVt(
    const std::vector<float>& gm, 
    const std::vector<float>& vgs,
    const std::vector<float>& ids
) {
    if (gm.size() != vgs.size() || gm.size() < 5) {
        return 0.0f;
    }
    
    // Find index of maximum Gm
    auto maxIt = std::max_element(gm.begin(), gm.end());
    size_t maxIdx = std::distance(gm.begin(), maxIt);
    float maxGm = *maxIt;
    
    if (maxGm <= 0 || maxIdx < 2 || maxIdx >= gm.size() - 2) {
        return 0.0f;
    }
    
    // At the max Gm point:
    // IDS = Gm * (VGS - Vt)
    // => Vt = VGS - IDS/Gm
    
    float vgsAtMax = vgs[maxIdx];
    float idsAtMax = ids[maxIdx];
    
    if (maxGm > 1e-12f) {
        float vt = vgsAtMax - (idsAtMax / maxGm);
        
        // Sanity check: Vt should be less than VGS at max Gm
        // and typically positive for NMOS
        if (vt > 0 && vt < vgsAtMax) {
            return vt;
        }
    }
    
    // Alternative: Use second derivative peak (more robust)
    std::vector<float> d2 = calculateGm(gm, vgs, GmConfig());
    auto max_d2 = std::max_element(d2.begin(), d2.end());
    size_t d2Idx = std::distance(d2.begin(), max_d2);
    
    if (d2Idx > 0 && d2Idx < vgs.size()) {
        return vgs[d2Idx];
    }
    return 0.0f;
}

// ============================================================================
// SS Calculation — Sliding-Window Linear Regression
// ============================================================================
//
// Strategy: for every candidate window [i, i+win) in the region where
// log10(Ids) is strictly increasing, perform a linear regression of
// log10(Ids) vs VGS. Keep the window that yields the highest R².
// SS = (1 / slope_best) × 1000 mV/dec.
//
// This replaces the brittle "longest run of consistent slopes" heuristic,
// which failed on saturated curves where the subthreshold region is short.

SSResult calculateSS(
    const std::vector<float>& ids,
    const std::vector<float>& vgs
) {
    SSResult result;

    if (ids.size() != vgs.size() || ids.size() < 10) {
        return result;
    }

    const size_t n = ids.size();

    // ── Step 1: build log10(Ids) array; mark valid subthreshold points ─────
    // Only consider points where |Ids| is in the subthreshold band [1e-8, 1e-4] A.
    // The 1k shunt allows clean readings down to ~10nA, where the true exponential
    // region lies before moving into moderate inversion near 1µA.
    const float IDS_FLOOR    = 5e-10f;  // absolute minimum to take log
    const float IDS_SUB_LOW  = 1e-8f;   // lower bound of subthreshold band (10 nA)
    const float IDS_SUB_HIGH = 1e-4f;   // upper bound of subthreshold band (100 µA)

    std::vector<float> logIds(n, 0.0f);
    std::vector<bool>  usable(n, false);

    // Light smoothing to reduce ADC spikes before taking log
    std::vector<float> idsSmooth = movingAverageSmooth(ids, 3);

    for (size_t i = 0; i < n; i++) {
        float val = fabsf(idsSmooth[i]);
        if (val > IDS_FLOOR) {
            logIds[i] = log10f(val);
            // Mark usable only if within subthreshold current band
            usable[i] = (val >= IDS_SUB_LOW && val <= IDS_SUB_HIGH);
        }
    }

    // ── Step 2: sliding-window regression ──────────────────────────────────
    // Window sizes to try (in number of points)
    const size_t MIN_WIN = 5;
    const size_t MAX_WIN = 20;

    float bestR2     = -1.0f;
    float bestSlope  =  0.0f;
    float bestIntercept = 0.0f;
    size_t bestWinStart = 0;
    size_t bestWinEnd   = 0;

    // Pre-allocate reusable buffers OUTSIDE the loop to avoid heap fragmentation
    std::vector<float> wx, wy;
    wx.reserve(MAX_WIN);
    wy.reserve(MAX_WIN);

    for (size_t win = MIN_WIN; win <= MAX_WIN && win <= n; win++) {
        for (size_t start = 0; start + win <= n; start++) {
            size_t end = start + win;  // exclusive

            // Reuse buffers — no heap allocation inside the hot loop
            wx.clear();
            wy.clear();

            for (size_t i = start; i < end; i++) {
                if (usable[i]) {
                    wx.push_back(vgs[i]);
                    wy.push_back(logIds[i]);
                }
            }

            if (wx.size() < MIN_WIN) continue;

            // Filter 1: log(Ids) must be net-increasing (subthreshold region)
            if (wy.back() <= wy.front()) continue;

            // Filter 2: require at least 0.5 decades of variation across the window.
            const float MIN_DELTA_DECADES = 0.5f;
            if ((wy.back() - wy.front()) < MIN_DELTA_DECADES) continue;

            float slope, intercept;
            float r2 = linearRegression(wx, wy, slope, intercept);

            // Filter 3: slope must be ≥ 1 dec/V → SS ≤ 1000 mV/dec.
            const float MIN_SLOPE_DEC_PER_V = 1.0f;
            if (slope < MIN_SLOPE_DEC_PER_V) continue;

            // Optimization target: the true subthreshold region is the STEEPEST exponential slope
            // before strong inversion. Ohmic leakage often has a smoother but gentler slope.
            // So we select the maximum slope that possesses a valid linear R² (>= 0.85).
            if (r2 >= 0.85f && slope > bestSlope) {
                bestR2        = r2;
                bestSlope     = slope;
                bestIntercept = intercept;
                bestWinStart  = start;
                bestWinEnd    = end - 1;  // inclusive
            }
        }
    }

    // ── Step 3: validate and produce result ────────────────────────────────

    if (bestR2 >= 0.85f && bestSlope > 1e-9f) {
        float ss_val = (1.0f / bestSlope) * 1000.0f;  // mV/dec

        // Physically plausible range: 60 mV/dec (ideal) … 1000 mV/dec
        if (ss_val >= 60.0f && ss_val <= 1000.0f) {
            result.ss_mVdec   = ss_val;
            result.valid      = true;
            result.regionStart = bestWinStart;
            result.regionEnd   = bestWinEnd;

            // Tangent line endpoints in log10(Ids) space
            result.x1 = vgs[bestWinStart];
            result.y1 = bestSlope * result.x1 + bestIntercept;
            result.x2 = vgs[bestWinEnd];
            result.y2 = bestSlope * result.x2 + bestIntercept;
        }
    }

    return result;
}


} // namespace math_engine
