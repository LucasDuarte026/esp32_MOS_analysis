#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MCP4725.h>
#include <driver/dac.h>

Adafruit_MCP4725 mcp;
Adafruit_ADS1115 ads;

// Configurações
const uint8_t DAC_INTERNAL_PIN = 25;
const uint8_t MCP4725_ADDR = 0x60;
const uint8_t ADS1115_ADDR = 0x48;

// Configurações Adicionais para Leitura Limpa
const uint16_t OVERSAMPLING_COUNT = 8; // Qtde de amostras para fazer a média
const uint32_t SETTLING_TIME_MS = 250;  // Tempo de estabilização do DAC antes de ler

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("\n\n--- INICIANDO TESTE DE HARDWARE (DACs e ADC) ---");
  Serial.printf("- Amostras por Leitura: %d\n", OVERSAMPLING_COUNT);
  Serial.printf("- Tempo de establizacao: %d ms\n", SETTLING_TIME_MS);
  
  Wire.begin();
  
  // 1. Inicializa DAC Interno
  dac_output_enable(DAC_CHANNEL_1); // GPIO 25
  dac_output_voltage(DAC_CHANNEL_1, 0);
  Serial.println("[OK] DAC Interno (GPIO 25) Pronto.");
  
  // 2. Inicializa MCP4725
  if (!mcp.begin(MCP4725_ADDR)) {
    Serial.println("[ERRO] MCP4725 (0x60) nao encontrado!");
  } else {
    mcp.setVoltage(0, false);
    Serial.println("[OK] MCP4725 (0x60) Pronto.");
  }
  
  // 3. Inicializa ADS1115
  if (!ads.begin(ADS1115_ADDR)) {
    Serial.println("[ERRO] ADS1115 (0x48) nao encontrado!");
  } else {
    // GAIN_TWOTHIRDS: +/- 6.144V (1 bit = 0.1875mV)
    ads.setGain(GAIN_TWOTHIRDS);
    Serial.println("[OK] ADS1115 (0x48) Pronto. Ganho = TWOTHIRDS (+/- 6.144V).");
  }
  
  delay(2000);
}

// Funcao auxiliar para tirar media
float ler_adc_media(uint8_t canal) {
  float soma = 0;
  for (int i = 0; i < OVERSAMPLING_COUNT; i++) {
    int16_t leitura_bruta = ads.readADC_SingleEnded(canal);
    // Para GAIN_TWOTHIRDS, 1 LSB = 0.1875mV = 0.0001875V
    soma += leitura_bruta * 0.0001875f;
    delay(2); // pequeno espaço entre leituras pro ADC converter
  }
  return soma / (float)OVERSAMPLING_COUNT;
}

void loop() {
  Serial.println("\n\n================================================");
  Serial.println("  INICIANDO VARREDURA - TESTE DRAIN (DAC Interno + A0)");
  Serial.println("================================================");
  
  // Zerar o MCP4725 enquanto testa o Interno
  mcp.setVoltage(0, false);
  
  // Sweep DAC Interno (Dreno) - 0 a 3.3V
  // Lendo da porta A0
  for (int step = 0; step <= 255; step += 15) { 
    // Escreve DAC Interno
    dac_output_voltage(DAC_CHANNEL_1, step);
    float v_esperada = (step / 255.0) * 3.3; 
    
    // Aguarda componente/amplificador estabilizar
    delay(SETTLING_TIME_MS); 
    
    // Ler ADC A0 com Media
    float v_lida = ler_adc_media(0);
    
    Serial.printf("DAC Int (Passo %3d) | V_esp: ~%.2fV | Lido A0 (Media de %d): %.3fV\n", 
                   step, v_esperada, OVERSAMPLING_COUNT, v_lida);
  }
  
  dac_output_voltage(DAC_CHANNEL_1, 0);
  delay(2000);
  
  Serial.println("\n\n================================================");
  Serial.println("  INICIANDO VARREDURA - TESTE GATE (MCP4725 + A1)");
  Serial.println("================================================");
  
  // Sweep MCP4725 (Gate) - 0 a 5V lendo da porta A1
  for (int step = 0; step <= 4095; step += 256) {
    // Escreve MCP4725
    mcp.setVoltage(step, false);
    float v_esperada = (step / 4095.0) * 5.0; 
    
    // Aguarda MCP estabilizar
    delay(SETTLING_TIME_MS);
    
    // Ler ADC A1 com Media
    float v_lida = ler_adc_media(1);
    
    Serial.printf("MCP4725 (Passo %4d) | V_esp: ~%.2fV | Lido A1 (Media de %d): %.3fV", 
                   step, v_esperada, OVERSAMPLING_COUNT, v_lida);
                   
    if (v_lida > 6.1) Serial.print(" [ALERTA: Perto da Saturacao do ADC!]");
    Serial.println();
  }
  
  mcp.setVoltage(0, false);
  
  Serial.println("\n--- Fim do ciclo. Aguardando 5 segundos para reiniciar... ---");
  delay(5000);
}
