# Módulo: Firmware Sources (src/)

> **Documentação de Referência - Spec-Driven Development (SDD)**  
> **Hierarquia:** Nível 4 e Nível 5 (Orquestração, Integração e Implementação C++)  
> **Status:** Ativo  

---

## 1. Objetivo do Módulo
Implementar as rotinas executáveis do caracterizador de MOSFETs na linguagem C++ (compilado para a arquitetura do ESP32 Xtensa LX6). O módulo gerencia a lógica de funcionamento, comunicação física com periféricos I2C, execução das tarefas do FreeRTOS, arquivos locais de medição na flash, e API web.

---

## 2. Responsabilidade Principal
Garantir o processamento concorrente seguro de requisições web, a execução rigorosa de varreduras elétricas em malha fechada sem interferência de timing, a gravação confiável de dados, e a entrega segura dos metadados extraídos dos transistores sob teste (DUT).

---

## 3. Funcionalidades Existentes
* **Interface Abstrata HAL**: Permite rodar o sistema com hardware real externo (I2C) ou simulação interna (fallback).
* **Varreduras em Loop Fechado**: Executa calibração dinâmica nos DACs de Gate e Dreno a cada etapa, reduzindo o erro a valores menores que $2\ \text{mV}$ para anular a degeneração de source.
* **Auto-Range e Proteção**: Protege o MOSFET contra sobrecorrente ($> 500\ \text{mA}$) e saturação analógica do LM358 ($> 3.3\ \text{V}$), chaveando os canais do ADC de A3 para A0 de forma autônoma.
* **Filtros Digitais e Oversampling**: Coleta múltiplas amostras no ADC (até 64x), ordenando-as com *Insertion Sort* e aplicando média centralizada (*Trimmed Mean*).
* **Streaming Direto para Flash**: Salva os pontos das varreduras em formato CSV diretamente no FAT interno (`FFat`) em tempo real.
* **Exposição de API HTTP REST**: Servidor web assíncrono para controle e monitoramento do estado da máquina de sweep.

---

## 4. Dependências Internas e Externas
* **Dependências Internas**:
  * [include/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/) (arquivos de cabeçalho de definição de constantes e tipos).
  * [src/web/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/web/) (código fonte frontend que é compilado e embarcado).
* **Dependências Externas**:
  * **Framework Arduino para ESP32** (sobre o ESP-IDF).
  * **Adafruit ADS1X15 Library**: Driver para comunicação com o ADC ADS1115.
  * **Adafruit MCP4725 Library**: Driver para controle do DAC MCP4725.
  * **ESPAsyncWebServer**: Servidor HTTP assíncrono não-bloqueante.
  * **ArduinoJson**: Serialização e parsing de payloads JSON da API.

---

## 5. Módulos Relacionados
* [include/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/): Mapeia as assinaturas das classes e configurações estáticas do firmware.
* [src/web/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/web/): Fornece a interface visual que se comunica via HTTP com a API REST implementada neste módulo.
* [scripts/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/scripts/): Contém o minificador e empacotador de frontend que injeta o arquivo `web_dashboard.h` neste módulo durante o pré-build.

---

## 6. Pontos de Entrada e Arquivos Críticos

### 6.1 Pontos de Entrada
* **`setup()` (em `main.cpp`)**: Ponto inicial do sistema. Inicializa os módulos de Wi-Fi, mDNS, FFat, barramento I2C, detecta os componentes físicos de hardware, inicia a tarefa de medição do FreeRTOS, e configura as rotas REST do servidor HTTP.
* **`loop()` (em `main.cpp`)**: Apenas monitora o watchdog básico do sistema, pois a execução ocorre através de tarefas agendadas em multithread pelo FreeRTOS.

### 6.2 Arquivos Críticos
1. **[main.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/main.cpp)**: Inicialização global do microcontrolador e definição das rotas de API HTTP.
2. **[mosfet_controller.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/mosfet_controller.cpp)**: Core do caracterizador. Controla a máquina de estados, o algoritmo de varredura (sweeps) e a malha de controle PID simples.
3. **[hardware_hal.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/hardware_hal.cpp)**: Abstração de hardware. Contém as leituras do barramento I2C, o algoritmo de *Oversampling & Trimmed Mean* e o chaveamento dinâmico de ganhos do ADS1115 (PGA).
4. **[math_engine.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/math_engine.cpp)**: Motor matemático para Savitzky-Golay, transcondutância, extrapolação de $V_{th}$ e regressão linear de $SS$.

---

## 7. Fluxos Importantes

### 7.1 Varredura de Tensão em Malha Fechada
Mapeia a iteração contínua de calibração para neutralizar a queda na resistência de shunt.

```
[Módulo MOSFETController]
  │
  ├──► 1. Calcula tensão alvo para VGS e VDS
  ├──► 2. Aplica primeira estimativa de tensão ao DAC (MCP4725)
  ├──► 3. Aguarda o tempo de acomodação (Settling Time)
  ├──► 4. Lê a tensão nos pinos através do ADC (ADS1115)
  ├──► 5. Calcula o erro sistemático nos terminais do DUT
  └──► 6. Ajusta o DAC recursivamente até erro < 2 mV (máx. 10 iterações)
```

### 7.2 Troca Automática de Faixa de Leitura (Auto-Range)
Gerenciado na rotina de leitura do ADC dentro do HAL para contornar a saturação do LM358 em 3.4 V:

```
[hardware_hal.cpp -> measureShuntSample()]
  │
  ├──► Lê canal A3 (amplificado 31.3x)
  ├──► Se A3 < 3.3V: retorna A3_leitura / 31.3039 (precisão de nA)
  └──► Se A3 >= 3.3V: chaveia canal físico para A0 (bruto, leitura direta)
```

---

## 8. Observações Técnicas e Débitos Identificados

* **🔒 Uso de Mutex no Barramento I2C**: Qualquer operação de barramento I2C feita pelo Core 0 (ex: checagem de hardware via REST API) ou Core 1 (medição ativa) DEVE ser envelopada com `HardwareHAL::getI2CMutex()`. A violação deste padrão corrompe a comunicação dos DACs e derruba o microcontrolador.
* **⚠️ Débito Técnico - Velocidade Máxima do ADC**: O ADS1115 opera a uma velocidade máxima teórica de 860 SPS. Coletar oversamplings de 64x no canal do shunt requer múltiplos milissegundos de barramento travado. Lógicas futuras devem avaliar amostragem paralela não-bloqueante por interrupção ou DMA, se suportado.
* **⚠️ Débito Técnico - Delay de Escrita no DAC**: Há necessidade de assegurar um delay de $0.5\ \text{ms}$ após atualizar a tensão do MCP4725 antes de executar uma leitura de realimentação no ADC, permitindo que a linha analógica estabilize eletricamente. Atualmente isso é feito por delay passivo no loop do controlador, necessitando de encapsulamento genérico direto no HAL.
* **⚠️ Débito Técnico - Desligamento do Wi-Fi na Medição**: A ativação de conexões Wi-Fi do ESP32 gera transientes de corrente elevados na linha comum de 5 V, injetando ruído magnético na medição analógica sensível de nA. Há planos futuros de desligar a pilha de rede Wi-Fi durante os sweeps ativos, reativando-a apenas para envio final de dados.
