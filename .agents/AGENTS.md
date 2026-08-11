# Diretrizes e Instruções de Agente — ESP32 MOSFET Analysis

> **Projeto:** Plataforma de Caracterização de MOSFETs (ESP32 + ADS1115 + WebUI)  
> **Modelo de Desenvolvimento:** Spec-Driven Development (SDD) Hierárquico  
> **Status:** Ativo  

---

## 1. Regras Obrigatórias de Desenvolvimento (Strict Rules)

### 1.1 Modelo Spec-Driven Bottom-Up (Hierarquia de Dependência)
Qualquer alteração em funcionalidade ou requisito deve propagar rigorosamente de baixo para cima:
1. **Física/Hardware**: Limites físicos, shunts, pinagem e conexões analógicas.
2. **Abstração (HAL)**: Leitura de barramento I2C, conversões analógicas, calibrações de sensores em [hardware_hal.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/hardware_hal.cpp).
3. **Controle/Matemática**: Lógica do sweep e motores em [mosfet_controller.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/mosfet_controller.cpp) e [math_engine.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/math_engine.cpp).
4. **Sistema/Orquestração**: FreeRTOS, FFat, Wi-Fi e logs em [main.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/main.cpp).
5. **API REST / Serviços**: Endpoints HTTP em [web_ui.cpp](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/web_ui.cpp).
6. **Interface (WebUI)**: HTML/JS/CSS em [src/web/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/web/).

### 1.2 Debug e Logging (Não Negociável)
* **Backend C++ (ESP32)**: Use a classe `AsyncLogger` com os níveis: `DEBUG`, `INFO`, `WARNING` e `ERROR`. O volume de logs serial é controlado dinamicamente pelo estado do pino físico **GPIO12** (GND = Modo debug ativo; flutuante = produção).
  ```cpp
  AsyncLogger::log(AsyncLogger::DEBUG, "webui::handleRequest: recebido parametro %d", val);
  ```
* **Frontend JavaScript**: **É proibido o uso de `console.log`**. Utilize a função de log estruturado `dbg('CATEGORIA', ...)` definida em `core.js`. Categorias válidas: `API`, `CSV`, `PLOT`, `UI`, `MATH`.
  ```javascript
  dbg('API', 'Iniciando varredura com config:', config);
  ```

### 1.3 Convenções e Padrões de Código
* **C++ (Firmware)**:
  * Todos os códigos devem estar organizados nos namespaces correspondentes: `hal::`, `mosfet::`, `webui::`, `math_engine::`.
  * Alocações dinâmicas na heap são proibidas durante medições ativas. Para servir arquivos estáticos grandes na WebUI, use `sendProgmemChunked()` para streaming em blocos.
  * O servidor web deve ser assíncrono e não-bloqueante (`ESPAsyncWebServer`), rodando fixado no Core 0 do ESP32.
  * Todas as operações I2C concorrentes devem ser protegidas pelo semáforo Mutex (`HardwareHAL::getI2CMutex()`).
* **JavaScript (Frontend)**:
  * O frontend reside em [src/web/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/web/) e está modularizado em quatro scripts: `core.js` (logs/toasts), `collection.js` (coleta/polling), `visualization.js` (Plotly/Math/CSV) e `email.js` (SMTP).
  * Lógica de manipulação de DOM deve sempre rodar dentro de listeners do evento `DOMContentLoaded`.
  * Toda ação assíncrona ou de erro deve fornecer toast feedback ao usuário via `showToast(msg, type)`.

### 1.4 Ciclo de Build e Versionamento
* **Injeção de Assets Web**: Arquivos HTML/CSS/JS são minificados e transformados em byte arrays estáticos de Flash pelo script de pre-build [scripts/embed_web.py](file:///home/luska/Documents/projects/esp32_mosfet_analysis/scripts/embed_web.py), gerando o arquivo header `src/generated/web_dashboard.h`. Não use SPIFFS/LittleFS para armazenar a UI principal.
* **Versionamento Semântico**: Mantido em [include/version.h](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/version.h) no formato `MAJOR.MINOR.SNAPSHOT`. Incremente `SNAPSHOT` a cada alteração lógica.

---

## 2. Arquitetura Física de Hardware

* **Controle de Gate ($V_{gs}$)**: DAC externo MCP4725 @ endereço **0x60** (12-bit, I2C).
* **Controle de Dreno ($V_{ds}$)**: DAC externo MCP4725 @ endereço **0x61** (12-bit, I2C) acoplado a um buffer de potência de ganho unitário baseado no Amp-Op **LT1013**.
* **Aquisição (ADC ADS1115 @ 0x48)**:
  * **A0**: Leitura bruta da queda no Shunt (para correntes elevadas).
  * **A1**: Leitura direta da tensão do Dreno ($V_d$) para realimentação.
  * **A2**: Leitura direta da tensão do Gate ($V_g$) para realimentação.
  * **A3**: Leitura amplificada da queda no Shunt ($31.3039\times$ via LT1013) para correntes infinitesimais em sublimiar.

---

## 3. Limites Físicos e Comportamentos Anômalos

* **Compensação do Resistor de Proteção de 4.7k**: A leitura de dreno ($A1$) possui um resistor de proteção de $4.7\ \text{k}\Omega$ em série. Isso gera erros de ganho e de offset no ADC corrigidos pelo modelo linear em HAL: $V_{real} = (V_{raw} \times 1.0015) + 0.0005\text{ V}$.
* **Saturação e Auto-Range**: O LM358 de amplificação do shunt satura próximo a $3.4\text{ V}$. O firmware deve chavear automaticamente de A3 para A0 quando a leitura ultrapassar o limiar de transição de $3.3\text{ V}$.
* **Auto-Zero (Tara)**: Ao iniciar varreduras, o controlador faz 64 leituras em standby de $A3$ e calcula a tara média para subtrair offsets residuais do LT1013/ADS1115 de leituras futuras.

---

## 4. Regras e Diagnósticos Específicos

Consulte os arquivos modulares em `.agents/rules/` para aprofundamento:
- [rules/development-guidelines.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/.agents/rules/development-guidelines.md): Diretrizes completas de arquitetura, coding style e pipelines de build.
- [rules/analog-diagnostics.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/.agents/rules/analog-diagnostics.md): Dossiê de diagnóstico analógico, mitigação de crosstalk/charge injection e calibração no ADS1115.
