#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MCP4725.h>
#include <Adafruit_ADS1X15.h>
#include <driver/dac.h>

// Configurações de Hardware
#define INTERNAL_DAC_CH DAC_CHANNEL_1 // GPIO 25
#define MCP4725_ADDR 0x60
#define ADS1115_ADDR 0x48

// Referências de Tensão (Ajuste se necessário)
#define VREF_INTERNAL 3.3
#define VREF_EXTERNAL 5.0 // Voltagem de alimentação do MCP4725

Adafruit_MCP4725 mcp;
Adafruit_ADS1115 ads;

void setup() {
    Serial.begin(115200);
    Wire.begin();

    // Inicializa MCP4725
    if (!mcp.begin(MCP4725_ADDR)) {
        Serial.println("\n[ERRO] MCP4725 não encontrado no endereço 0x60!");
        while(1) delay(10);
    }

    // Inicializa ADS1115
    ads.setGain(GAIN_TWOTHIRDS); // ±6.144V
    if (!ads.begin(ADS1115_ADDR)) {
        Serial.println("\n[ERRO] ADS1115 não encontrado no endereço 0x48!");
        while(1) delay(10);
    }

    // Inicializa DAC Interno
    dac_output_enable(INTERNAL_DAC_CH);
    dac_output_voltage(INTERNAL_DAC_CH, 0);

    Serial.println("\n╔══════════════════════════════════╗");
    Serial.println("║      Teste Manual de DACs        ║");
    Serial.println("║  Interno: GPIO25 (8-bit) -> A1   ║");
    Serial.println("║  Externo: MCP4725 (12-bit) -> A2 ║");
    Serial.println("╚══════════════════════════════════╝");
    Serial.println("\nInstruções: Digite os dois valores separados por espaço (ex: 1.0 2.25) e aperte ENTER.");
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
        Serial.print("\nDigite as tensões [Interno Externo] (Ex: 1.2 2.5): ");
        showPrompt = false;
    }

    while (Serial.available() > 0) {
        char c = Serial.read();

        if (c == '\n' || c == '\r') {
            if (inputBuffer.length() > 0) {
                Serial.println();
                
                float voltInt = 0, voltExt = 0;
                int readCount = sscanf(inputBuffer.c_str(), "%f %f", &voltInt, &voltExt);

                if (readCount >= 2) {
                    Serial.printf("  > Setpoint: Interno=%.3fV, Externo=%.3fV\n", voltInt, voltExt);

                    // --- DAC Interno (8-bit) ---
                    int valInt = (int)((voltInt / VREF_INTERNAL) * 255);
                    valInt = constrain(valInt, 0, 255);
                    dac_output_voltage(INTERNAL_DAC_CH, (uint8_t)valInt);

                    // --- DAC Externo (12-bit) ---
                    int valExt = (int)((voltExt / VREF_EXTERNAL) * 4095);
                    valExt = constrain(valExt, 0, 4095);
                    mcp.setVoltage((uint16_t)valExt, false);

                    delay(500); // Aguarda estabilização

                    // --- Leitura Real via ADS1115 ---
                    float readA1 = getAVGRading(1); // Leitura do DAC Interno
                    float readA2 = getAVGRading(2); // Leitura do DAC Externo

                    Serial.printf("  [LIDO]    Interno(A1)=%.3fV, Externo(A2)=%.3fV\n", readA1, readA2);
                    Serial.println("  [OK] Concluído.");
                } else {
                    Serial.println("  [ERRO] Formato inválido.");
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
