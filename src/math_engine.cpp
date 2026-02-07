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
// SS Calculation with Tangent Line
// ============================================================================

SSResult calculateSS(
    const std::vector<float>& ids, 
    const std::vector<float>& vgs
) {
    SSResult result;
    
    if (ids.size() != vgs.size() || ids.size() < 10) {
        return result;
    }
    
    size_t n = ids.size();
    
    // Step 0: Smooth IDs to reduce noise
    std::vector<float> idsSmooth = movingAverageSmooth(ids, 5);
    
    // Step 1: Calculate local slopes (d(log10(Ids))/dVgs)
    std::vector<float> slopes(n, 0.0f);
    std::vector<bool> valid(n, false);
    
    for (size_t i = 1; i < n - 1; i++) {
        float idsPrev = fabs(idsSmooth[i - 1]);
        float idsNext = fabs(idsSmooth[i + 1]);
        
        // Skip near-zero currents (noise floor)
        if (idsPrev < 1e-12f || idsNext < 1e-12f) continue;
        
        float dVgs = vgs[i + 1] - vgs[i - 1];
        float dLogIds = log10f(idsNext) - log10f(idsPrev);
        
        // Positive slope = current increasing (subthreshold region)
        if (fabs(dVgs) > 1e-9f && dLogIds > 0.01f) {
            slopes[i] = dLogIds / dVgs;  // decades/V
            valid[i] = (slopes[i] > 0.5f);  // At least 0.5 dec/V
        }
    }
    
    // Step 2: Find longest run of consistent slopes
    size_t bestStart = 0, bestLen = 0;
    size_t currStart = 0, currLen = 0;
    
    for (size_t i = 1; i < n; i++) {
        if (!valid[i]) {
            if (currLen > bestLen) {
                bestStart = currStart;
                bestLen = currLen;
            }
            currLen = 0;
            currStart = i + 1;
            continue;
        }
        
        if (currLen == 0 || !valid[i - 1]) {
            currStart = i;
            currLen = 1;
        } else {
            float ratio = slopes[i] / slopes[i - 1];
            // Check consistency (within 2x)
            if (ratio > 0.5f && ratio < 2.0f) {
                currLen++;
            } else {
                if (currLen > bestLen) {
                    bestStart = currStart;
                    bestLen = currLen;
                }
                currStart = i;
                currLen = 1;
            }
        }
    }
    if (currLen > bestLen) {
        bestStart = currStart;
        bestLen = currLen;
    }
    
    // Step 3: Perform linear regression on best region
    if (bestLen >= 3) {
        std::vector<float> x, y;
        for (size_t i = bestStart; i < bestStart + bestLen && i < n; i++) {
            float val = fabs(ids[i]);
            if (val > 1e-15f) {
                x.push_back(vgs[i]);
                y.push_back(log10f(val));
            }
        }
        
        if (x.size() >= 3) {
            float slope, intercept;
            float r2 = linearRegression(x, y, slope, intercept);
            
            if (fabs(slope) > 1e-9f && r2 > 0.9f) {
                // SS = (1/slope) * 1000 mV/dec
                float ss_val = (1.0f / fabs(slope)) * 1000.0f;
                
                // Sanity check (60-1000 mV/dec is reasonable)
                if (ss_val >= 60.0f && ss_val <= 2000.0f) {
                    result.ss_mVdec = ss_val;
                    result.valid = true;
                    result.regionStart = bestStart;
                    result.regionEnd = bestStart + bestLen - 1;
                    
                    // Calculate tangent line coordinates
                    // Use first and last points of the regression region
                    result.x1 = x.front();
                    result.y1 = slope * result.x1 + intercept;
                    result.x2 = x.back();
                    result.y2 = slope * result.x2 + intercept;
                }
            }
        }
    }
    
    return result;
}

} // namespace math_engine
