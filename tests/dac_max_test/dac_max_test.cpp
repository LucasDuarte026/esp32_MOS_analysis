#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_ADS1X15.h>
#include <driver/dac.h>

// ============================================================================
// >>>  USER CONFIG: change only this value  <<<
// ============================================================================
#define STEP_V          0.001f     // Voltage step per sample (V)
#define FINAL_V         5.12f      // End voltage (clamped to MAX_VOLTAGE = 5.12V)
#define SETTLE_MS       5        // Settling time between steps (ms)

// ============================================================================
// Hardware Configuration (do not change unless rewiring)
// ============================================================================
#define MCP4725_VD_ADDR 0x61
#define MCP4725_VG_ADDR 0x60
#define ADS1115_ADDR    0x48

#define MAX_VOLTAGE     5.12f      // Absolute ceiling (Vref of the 5V bus)
#define DAC_MAX_CODE    4095

// ADS1115: Fixed gain GAIN_TWOTHIRDS → FSR ±6.144 V → 187.5 µV/bit
#define ADC_FSR         6.144f
#define ADC_MAX_RAW     32767.0f

// ============================================================================
// Globals
// ============================================================================
Adafruit_MCP4725 mcp_vd;
Adafruit_MCP4725 mcp_vg;
Adafruit_ADS1115 ads;

float targetVoltage = 0.0f;
float ceilingV = 0.0f;   // computed in setup: min(FINAL_V, MAX_VOLTAGE)
bool  finished = false;

// ============================================================================
// Helpers
// ============================================================================
float rawToVolts(int16_t raw) {
    if (raw < 0) raw = 0;
    return (float)raw * (ADC_FSR / ADC_MAX_RAW);
}

void updateDACs(float voltage) {
    if (voltage > MAX_VOLTAGE) voltage = MAX_VOLTAGE;
    if (voltage < 0.0f)       voltage = 0.0f;
    uint16_t val = (uint16_t)((voltage / MAX_VOLTAGE) * DAC_MAX_CODE);
    mcp_vd.setVoltage(val, false);
    mcp_vg.setVoltage(val, false);
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    Wire.begin();

    if (!mcp_vd.begin(MCP4725_VD_ADDR))
        Serial.printf("# Error: MCP4725 VD (0x%02X) NOT FOUND!\n", MCP4725_VD_ADDR);
    else
        Serial.printf("# Found: MCP4725 VD at 0x%02X\n", MCP4725_VD_ADDR);

    if (!mcp_vg.begin(MCP4725_VG_ADDR))
        Serial.printf("# Error: MCP4725 VG (0x%02X) NOT FOUND!\n", MCP4725_VG_ADDR);
    else
        Serial.printf("# Found: MCP4725 VG at 0x%02X\n", MCP4725_VG_ADDR);

    if (!ads.begin(ADS1115_ADDR))
        Serial.println("# Error: ADS1115 not found!");
    else
        Serial.printf("# Found: ADS1115 at 0x%02X\n", ADS1115_ADDR);

    ads.setGain(GAIN_TWOTHIRDS);
    ads.setDataRate(RATE_ADS1115_860SPS);

    ceilingV = (FINAL_V < MAX_VOLTAGE) ? FINAL_V : MAX_VOLTAGE;

    int totalSteps = (int)(ceilingV / STEP_V) + 1;
    Serial.printf("# Vref = %.2f V | Step = %.3f V | Final = %.3f V | Points = %d | Settle = %d ms\n",
                  MAX_VOLTAGE, STEP_V, ceilingV, totalSteps, SETTLE_MS);
    Serial.printf("# ADC: GAIN_TWOTHIRDS | FSR = %.3f V | Res = %.1f uV/bit\n",
                  ADC_FSR, (ADC_FSR / ADC_MAX_RAW) * 1e6f);

    Serial.println("Timestamp(ms),Target(V),VD_V,VG_V,A3_V,VD_Raw,VG_Raw,A3_Raw");
}

// ============================================================================
// Main Loop — simple ramp: 0 → MAX_VOLTAGE in STEP_V increments
// ============================================================================
void loop() {
    if (finished) return;

    // Write target to both DACs
    updateDACs(targetVoltage);
    delay(SETTLE_MS);

    // Read ADC: A1 = VD, A2 = VG, A3 = amplified shunt (all fixed gain)
    int16_t adc1 = ads.readADC_SingleEnded(1);
    int16_t adc2 = ads.readADC_SingleEnded(2);
    int16_t adc3 = ads.readADC_SingleEnded(3);

    Serial.printf("%lu,%.3f,%.4f,%.4f,%.4f,%d,%d,%d\n",
                  millis(), targetVoltage,
                  rawToVolts(adc1), rawToVolts(adc2), rawToVolts(adc3),
                  adc1, adc2, adc3);

    // Advance
    if (targetVoltage >= ceilingV) {
        updateDACs(0.0f);
        Serial.println("# Done. DACs -> 0V.");
        finished = true;
        return;
    }

    targetVoltage += STEP_V;
    if (targetVoltage > ceilingV) targetVoltage = ceilingV;
}
