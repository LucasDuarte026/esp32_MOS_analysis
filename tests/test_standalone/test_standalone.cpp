#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MCP4725.h>
#include <driver/dac.h>

Adafruit_MCP4725 mcp;
Adafruit_ADS1115 ads;

// Hardware Config
const uint8_t DAC_VD_PIN   = 25; // Internal DAC CH1 (VDS)
const uint8_t MCP4725_ADDR = 0x60; // External DAC (VGS)
const uint8_t ADS1115_ADDR = 0x48; // External ADC

const uint16_t OVERSAMPLING_COUNT = 16;
const uint32_t SETTLING_TIME_MS   = 100;

float ler_adc_media(uint8_t canal) {
  float soma = 0;
  for (int i = 0; i < OVERSAMPLING_COUNT; i++) {
    int16_t leitura_bruta = ads.readADC_SingleEnded(canal);
    // GAIN_TWOTHIRDS: 1 LSB = 0.1875mV = 0.0001875V
    soma += (leitura_bruta > 0 ? leitura_bruta : 0) * 0.0001875f;
    delay(1);
  }
  return soma / (float)OVERSAMPLING_COUNT;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("\n\n--- INICIANDO DIAGNOSTICO DE SATURACAO DAC/ADC ---");
  Wire.begin();
  
  // 1. Initialise VD DAC (Internal CH1)
  dac_output_enable(DAC_CHANNEL_1);
  dac_output_voltage(DAC_CHANNEL_1, 0);
  Serial.println("[OK] VD DAC Interno (GPIO 25) Pronto.");
  
  // 2. Initialise VG DAC (MCP4725)
  if (!mcp.begin(MCP4725_ADDR)) {
    Serial.println("[ERRO] MCP4725 (0x60) nao encontrado!");
    while(1) delay(100);
  }
  mcp.setVoltage(0, false);
  Serial.println("[OK] VG DAC MCP4725 (0x60) Pronto.");
  
  // 3. Initialise ADC (ADS1115)
  if (!ads.begin(ADS1115_ADDR)) {
    Serial.println("[ERRO] ADS1115 (0x48) nao encontrado!");
    while(1) delay(100);
  }
  ads.setGain(GAIN_TWOTHIRDS); // +/- 6.144V FSR
  ads.setDataRate(RATE_ADS1115_860SPS);
  Serial.println("[OK] ADS1115 (0x48) Pronto. Ganho = TWOTHIRDS (+/- 6.144V).");
  
  Serial.println("\nIniciando testes em 3 segundos...");
  delay(3000);
}

void loop() {
  mcp.setVoltage(0, false);
  dac_output_voltage(DAC_CHANNEL_1, 0);
  delay(1000);

  Serial.println("\n================================================");
  Serial.println("  TESTE 1: Varredura VD (DAC Interno -> Lendo A1)");
  Serial.println("================================================");
  Serial.println("Alvo_Esp(V) | Enviado_DAC | Lido_A1(V) | Erro(V)");
  
  // ESP32 DAC is 8-bit (0-255), approx 0-3.3V
  for (int step = 0; step <= 255; step += 15) { 
    dac_output_voltage(DAC_CHANNEL_1, step);
    float v_esperada = (step / 255.0f) * 3.3f; 
    delay(SETTLING_TIME_MS); 
    
    // Ler ADC canal A1 (VD)
    float v_lida = ler_adc_media(1);
    float erro = v_esperada - v_lida;
    
    Serial.printf("  %5.2f     |     %3d     |   %5.3f    | %5.3f\n", 
                   v_esperada, step, v_lida, erro);
  }
  
  dac_output_voltage(DAC_CHANNEL_1, 0);
  delay(1000);
  
  Serial.println("\n================================================");
  Serial.println("  TESTE 2: Varredura VG (MCP4725 -> Lendo A2)");
  Serial.println("================================================");
  Serial.println("Alvo_Esp(V) | Enviado_DAC | Lido_A2(V) | Erro(V)");
  
  // MCP4725 is 12-bit (0-4095), approx 0-4.9V
  for (int step = 0; step <= 4095; step += 256) {
    mcp.setVoltage(step, false);
    float v_esperada = (step / 4095.0f) * 4.9f; // Assumindo FSR de 4.9V
    
    delay(SETTLING_TIME_MS);
    
    // Ler ADC canal A2 (VG)
    float v_lida = ler_adc_media(2);
    float erro = v_esperada - v_lida;
    
    Serial.printf("  %5.2f     |    %4d     |   %5.3f    | %5.3f", 
                   v_esperada, step, v_lida, erro);
                   
    if (v_esperada > 3.0f && v_lida < 3.4f) {
        Serial.print("  <-- SATURACAO DETECTADA?");
    }
    Serial.println();
  }
  
  mcp.setVoltage(0, false);
  
  Serial.println("\n--- Fim do ciclo. Repetindo em 10s ---");
  delay(10000);
}
