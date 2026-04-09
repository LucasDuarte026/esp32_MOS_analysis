#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_ADS1X15.h>

// Hardware Configuration
#define MCP4725_VD_ADDR 0x61 // External DAC for VD (Drain)
#define MCP4725_VG_ADDR 0x60 // External DAC for VG (Gate)
#define ADS1115_ADDR 0x48

// Reference Voltages
#define VCC_EXTERNAL 5.12f // User requested 5.12V
#define FLOAT_ERROR_MARGIN 0.001f

// Feedback Loop Configuration
const float VD_ERROR_TOLERANCE = 0.001f; // 1mV
const float VG_ERROR_TOLERANCE = 0.001f; // 1mV
const int MAX_RETRIES = 10;

enum TestState {
    STATE_HEADER,
    STATE_STEPS,
    STATE_RAMP,
    STATE_FINISHED
};

TestState currentState = STATE_HEADER;
float targetVoltage = 0.0;
unsigned long stateStartTime = 0;
int stepIndex = 0;
const float steps[] = {0.0, 1.0, 2.0, 3.0, 4.0, 5.12};
const int numSteps = sizeof(steps) / sizeof(steps[0]);
const unsigned long stepDuration = 300; // 300ms per step

Adafruit_MCP4725 mcp_vd;
Adafruit_MCP4725 mcp_vg;
Adafruit_ADS1115 ads;

// Timing & Logic
#define SAMPLE_DELAY_MS 1 // ms per point (controllable delay)
unsigned long lastSampleTime = 0;
const unsigned long sampleInterval = SAMPLE_DELAY_MS;

// Globals for storing the final results of the control loop
float final_vd_v = 0.0;
float final_vg_v = 0.0;
float final_err_vd = 0.0;
float final_err_vg = 0.0;
int16_t final_adc_vd = 0;
int16_t final_adc_vg = 0;

void controlLoopDACs(float target) {
    float dac_vd_setpoint = target; // Initial guess
    float dac_vg_setpoint = target;
    
    // Quick escape for 0V
    if (target <= 0.001f) {
        mcp_vd.setVoltage(0, false);
        mcp_vg.setVoltage(0, false);
        delay(5);
        final_adc_vd = ads.readADC_SingleEnded(1);
        final_adc_vg = ads.readADC_SingleEnded(2);
        final_vd_v = ads.computeVolts(final_adc_vd);
        final_vg_v = ads.computeVolts(final_adc_vg);
        final_err_vd = target - final_vd_v;
        final_err_vg = target - final_vg_v;
        return;
    }

    for (int i = 0; i < MAX_RETRIES; i++) {
        // Compute DAC values based on the CURRENT setpoints
        int val_vd = (int)((dac_vd_setpoint / VCC_EXTERNAL) * 4095);
        int val_vg = (int)((dac_vg_setpoint / VCC_EXTERNAL) * 4095);
        
        mcp_vd.setVoltage(constrain(val_vd, 0, 4095), false);
        mcp_vg.setVoltage(constrain(val_vg, 0, 4095), false);
        
        // Wait for DACs to settle and circuit to respond
        delay(5); 

        // Read ADC
        final_adc_vd = ads.readADC_SingleEnded(1);
        final_adc_vg = ads.readADC_SingleEnded(2);
        final_vd_v = ads.computeVolts(final_adc_vd);
        final_vg_v = ads.computeVolts(final_adc_vg);
        
        // Calculate Error (Target - Actual)
        final_err_vd = target - final_vd_v;
        final_err_vg = target - final_vg_v;
        
        bool ok_vd = abs(final_err_vd) <= VD_ERROR_TOLERANCE;
        bool ok_vg = abs(final_err_vg) <= VG_ERROR_TOLERANCE;
        
        if (ok_vd && ok_vg) {
            break; // Controlled successfully!
        }
        
        // Adjust setpoints proportionally (Gain = 1.0)
        // Since error = Target - Actual, if actual is lower, error > 0, we add positive error to setpoint.
        if (!ok_vd) dac_vd_setpoint += final_err_vd;
        if (!ok_vg) dac_vg_setpoint += final_err_vg;
        
        // Clamp setpoints to physical limits
        if (dac_vd_setpoint < 0) dac_vd_setpoint = 0;
        if (dac_vd_setpoint > 5.12f) dac_vd_setpoint = 5.12f;
        
        if (dac_vg_setpoint < 0) dac_vg_setpoint = 0;
        if (dac_vg_setpoint > 5.12f) dac_vg_setpoint = 5.12f;
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Initialize MCP4725 VD (0x61)
    if (!mcp_vd.begin(MCP4725_VD_ADDR)) {
        Serial.printf("# Error: MCP4725 VD (0x%02X) NOT FOUND!\n", MCP4725_VD_ADDR);
    } else {
        Serial.printf("# Found: MCP4725 VD at 0x%02X\n", MCP4725_VD_ADDR);
    }

    // Initialize MCP4725 VG (0x60)
    if (!mcp_vg.begin(MCP4725_VG_ADDR)) {
        Serial.printf("# Error: MCP4725 VG (0x%02X) NOT FOUND!\n", MCP4725_VG_ADDR);
    } else {
        Serial.printf("# Found: MCP4725 VG at 0x%02X\n", MCP4725_VG_ADDR);
    }

    // Initialize ADS1115
    if (!ads.begin(ADS1115_ADDR)) {
        Serial.println("# Error: ADS1115 not found!");
    }
    ads.setGain(GAIN_TWOTHIRDS); // +/- 6.144V

    stateStartTime = millis();
}

void loop() {
    unsigned long now = millis();

    if (now - lastSampleTime >= sampleInterval) {
        lastSampleTime = now;

        switch (currentState) {
            case STATE_HEADER:
                Serial.println("Timestamp(ms),Target(V),VD_V(0x61),VG_V(0x60),VD_Raw,VG_Raw,VD_Err(V),VG_Err(V)");
                currentState = STATE_STEPS;
                stateStartTime = now;
                return; // Delay next read/print to next loop

            case STATE_STEPS:
                targetVoltage = steps[stepIndex];
                controlLoopDACs(targetVoltage);

                if (now - stateStartTime >= stepDuration) {
                    stepIndex++;
                    stateStartTime = now;
                    if (stepIndex >= numSteps) {
                        currentState = STATE_RAMP;
                        targetVoltage = 0.0;
                    }
                }
                break;

            case STATE_RAMP:
                controlLoopDACs(targetVoltage);
                if (targetVoltage >= (5.12f - FLOAT_ERROR_MARGIN)) {
                    currentState = STATE_FINISHED;
                }
                break;

            case STATE_FINISHED:
                // Test completed, maintain 0V or just stop
                controlLoopDACs(0.0);
                return; 
        }

        // Print CSV Line using the results stored in globals by the control loop
        Serial.print(now);
        Serial.print(",");
        Serial.print(targetVoltage, 3);
        Serial.print(",");
        Serial.print(final_vd_v, 4);
        Serial.print(",");
        Serial.print(final_vg_v, 4);
        Serial.print(",");
        Serial.print(final_adc_vd);
        Serial.print(",");
        Serial.print(final_adc_vg);
        Serial.print(",");
        Serial.print(final_err_vd, 4);
        Serial.print(",");
        Serial.println(final_err_vg, 4);

        // Advance ramp voltage AFTER printing
        if (currentState == STATE_RAMP) {
            targetVoltage += 0.050f; // 50mV step
            if (targetVoltage > (5.12f - FLOAT_ERROR_MARGIN)) targetVoltage = 5.12f;
        }
    }
}
