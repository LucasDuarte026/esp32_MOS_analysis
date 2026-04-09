#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_ADS1X15.h>

// Configurações de Hardware
#define MCP4725_VG_ADDR 0x60 // Gate Voltage (VGS) - ADDR pin -> GND
#define MCP4725_VD_ADDR 0x61 // Drain Voltage (VDS) - ADDR pin -> VCC
#define ADS1115_ADDR    0x48

// Referências de Tensão
#define VREF_EXTERNAL 5.0 // Voltagem de alimentação dos MCP4725 (VDD)

Adafruit_MCP4725 mcpVG; // Gate
Adafruit_MCP4725 mcpVD; // Drain
Adafruit_ADS1115 ads;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Inicializa MCP4725 VG (0x60)
    if (!mcpVG.begin(MCP4725_VG_ADDR)) {
        Serial.println("\n[ERRO] MCP4725 VG não encontrado no endereço 0x60!");
        while(1) delay(10);
    }

    // Inicializa MCP4725 VD (0x61)
    if (!mcpVD.begin(MCP4725_VD_ADDR)) {
        Serial.println("\n[ERRO] MCP4725 VD não encontrado no endereço 0x61!");
        while(1) delay(10);
    }

    // Inicializa ADS1115
    ads.setGain(GAIN_TWOTHIRDS); // ±6.144V
    if (!ads.begin(ADS1115_ADDR)) {
        Serial.println("\n[ERRO] ADS1115 não encontrado no endereço 0x48!");
        while(1) delay(10);
    }

    Serial.println("\n╔══════════════════════════════════╗");
    Serial.println("║      Teste Manual de DACs        ║");
    Serial.println("║  VG: MCP4725 (0x60) -> ADS A2    ║");
    Serial.println("║  VD: MCP4725 (0x61) -> ADS A1    ║");
    Serial.println("╚══════════════════════════════════╝");
    Serial.println("\nInstruções: Digite as tensões VG e VD separadas por espaço (ex: 1.5 3.3) e aperte ENTER.");
}

String inputBuffer = "";
bool showPrompt = true;

float getAVGRading(int channel) {
    long sum = 0;
    for (int i=0; i<10; i++) {
        sum += ads.readADC_SingleEnded(channel);
        delay(5);
    }
    return ads.computeVolts(sum / 10);
}

void loop() {
    if (showPrompt) {
        Serial.print("\nDigite as tensões [VG VD] (Ex: 1.5 3.3): ");
        showPrompt = false;
    }

    while (Serial.available() > 0) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                Serial.println();
                
                float voltVG = 0, voltVD = 0;
                int readCount = sscanf(inputBuffer.c_str(), "%f %f", &voltVG, &voltVD);

                if (readCount >= 2) {
                    Serial.printf("  > Setpoint: VG=%.3fV, VD=%.3fV\n", voltVG, voltVD);

                    // --- DAC Gate (VG - 0x60) ---
                    int valVG = (int)((voltVG / VREF_EXTERNAL) * 4095);
                    valVG = constrain(valVG, 0, 4095);
                    mcpVG.setVoltage((uint16_t)valVG, false);

                    // --- DAC Drain (VD - 0x61) ---
                    int valVD = (int)((voltVD / VREF_EXTERNAL) * 4095);
                    valVD = constrain(valVD, 0, 4095);
                    mcpVD.setVoltage((uint16_t)valVD, false);

                    delay(500); // Aguarda estabilização

                    // --- Leitura Real via ADS1115 ---
                    // Mapping: A1=VD_Actual, A2=VG_Actual
                    float readVG = getAVGRading(2); // A2: Measured Gate Voltage
                    float readVD = getAVGRading(1); // A1: Measured Drain Voltage

                    Serial.printf("  [LIDO]    VG(A2)=%.3fV, VD(A1)=%.3fV\n", readVG, readVD);
                    Serial.println("  [OK] Concluído.");
                } else {
                    Serial.println("  [ERRO] Formato inválido. Use: float float");
                }
                
                inputBuffer = "";
                showPrompt = true;
            }
        } 
        else if (c == 8 || c == 127) {
            if (inputBuffer.length() > 0) {
                inputBuffer.remove(inputBuffer.length() - 1);
                Serial.print("\b \b"); 
            }
        }
        else if (isprint(c)) {
            inputBuffer += c;
            Serial.print(c); 
        }
    }
    delay(10);
}
