#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_ADS1X15.h>
#include <driver/dac.h>

// Hardware Configuration
#define INTERNAL_DAC_CH DAC_CHANNEL_1 // GPIO 25
#define MCP4725_ADDR 0x60
#define ADS1115_ADDR 0x48

// Reference Voltages
#define VCC_INTERNAL 3.3
#define VCC_EXTERNAL 4.6 // User reported 4.6V effectively

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
const float steps[] = {0.0, 1.0, 2.0, 3.0};
const int numSteps = sizeof(steps) / sizeof(steps[0]);
const unsigned long stepDuration = 300; // 300ms per step (1.2s total)

Adafruit_MCP4725 mcp;
Adafruit_ADS1115 ads;

// Timing & Logic
unsigned long lastSampleTime = 0;
const unsigned long sampleInterval = 25; // 25ms (40Hz)

void updateDACs(float voltage) {
    // Internal DAC (8-bit: 0-255) -> Always 3.3V reference
    int internalVal = (int)((voltage / VCC_INTERNAL) * 255);
    if (internalVal > 255) internalVal = 255;
    if (internalVal < 0) internalVal = 0;
    dac_output_voltage(INTERNAL_DAC_CH, internalVal);

    // External MCP4725 (12-bit: 0-4095) -> Follows VCC_EXTERNAL
    int externalVal = (int)((voltage / VCC_EXTERNAL) * 4095);
    if (externalVal > 4095) externalVal = 4095;
    if (externalVal < 0) externalVal = 0;
    mcp.setVoltage(externalVal, false);
}

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Initialize MCP4725
    if (!mcp.begin(MCP4725_ADDR)) {
        Serial.println("# Error: MCP4725 not found!");
    }

    // Initialize ADS1115
    if (!ads.begin(ADS1115_ADDR)) {
        Serial.println("# Error: ADS1115 not found!");
    }
    ads.setGain(GAIN_TWOTHIRDS); // +/- 6.144V (Set AFTER begin for safety)

    // Initialize Internal DAC
    dac_output_enable(INTERNAL_DAC_CH);

    stateStartTime = millis();
}

void loop() {
    unsigned long now = millis();

    if (now - lastSampleTime >= sampleInterval) {
        lastSampleTime = now;

        switch (currentState) {
            case STATE_HEADER:
                Serial.println("Timestamp(ms),Target(V),Int_V,Ext_V,Int_Raw,Ext_Raw");
                currentState = STATE_STEPS;
                stateStartTime = now;
                break;

            case STATE_STEPS:
                targetVoltage = steps[stepIndex];
                updateDACs(targetVoltage);

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
                updateDACs(targetVoltage);
                targetVoltage += 0.025; // 25mV step (assuming user meant V)
                if (targetVoltage > 3.3) {
                    targetVoltage = 3.3;
                    updateDACs(targetVoltage);
                    currentState = STATE_FINISHED;
                }
                break;

            case STATE_FINISHED:
                // Test completed, maintain 0V or just stop
                updateDACs(0.0);
                return; 
        }

        // Read ADC Channels
        // A1 = Internal DAC, A2 = External DAC
        int16_t adc1 = ads.readADC_SingleEnded(1);
        int16_t adc2 = ads.readADC_SingleEnded(2);

        float vInternal = ads.computeVolts(adc1);
        float vExternal = ads.computeVolts(adc2);

        // Print CSV Line
        Serial.print(now);
        Serial.print(",");
        Serial.print(targetVoltage, 3);
        Serial.print(",");
        Serial.print(vInternal, 4);
        Serial.print(",");
        Serial.print(vExternal, 4);
        Serial.print(",");
        Serial.print(adc1);
        Serial.print(",");
        Serial.println(adc2);
    }
}
