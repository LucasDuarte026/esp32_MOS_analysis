# Arquitetura do Sistema (System Architecture)

> **Documentação de Referência - Spec-Driven Development (SDD)**  
> **Versão:** 10.0.0  
> **Status:** Ativo  
> **Data de Atualização:** Junho de 2026  

---

## 1. Visão Arquitetural e Fluxo Bottom-Up (De Baixo para Cima)

O projeto é estruturado em uma arquitetura hierárquica baseada no princípio de **Propagação de Especificações de Baixo para Cima (Bottom-Up Spec Propagation)**. O hardware analógico e a física de semicondutores ditam as regras na base, e essas restrições sobem nível a nível pelas camadas de abstração até alcançar a interface do usuário no navegador.

```
┌────────────────────────────────────────────────────────────────────────┐
│ 6. Apresentação (src/web/ - index.html, JS, CSS)                      │  ◄── Cabeça
├────────────────────────────────────────────────────────────────────────┤
│ 5. API e REST (src/web_ui.cpp, src/email_manager.cpp)                  │
├────────────────────────────────────────────────────────────────────────┤
│ 4. Orquestração e SO (src/main.cpp, FreeRTOS, FFat, WiFi, Logs)        │
├────────────────────────────────────────────────────────────────────────┤
│ 3. Controle e Matemática (src/mosfet_controller.cpp, math_engine.cpp)  │
├────────────────────────────────────────────────────────────────────────┤
│ 2. Abstração de Hardware - HAL (src/hardware_hal.cpp)                 │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Física e Componentes (Shunts, DACs, ADCs, Op-Amps, DUT)             │  ◄── Mãos
└────────────────────────────────────────────────────────────────────────┘
```

> [!IMPORTANT]
> **Regra Hierárquica de Mudança:**  
> Sempre que um requisito ou componente físico mudar (ex: alteração no resistor Shunt, ganho do amplificador LT1013 ou comportamento de crosstalk no ADS1115), a modificação deve ser documentada e implementada **da base para o topo**.  
> *Muda-se a especificação física -> Atualiza-se o HAL -> Ajusta-se o Controle/PID -> Adapta-se a API -> Reconfigura-se o Frontend.*

---

## 2. Separação de Responsabilidades por Camadas

### Camada 1: Física e Componentes (Mãos)
* **Responsabilidade**: Interface analógica com o MOSFET.
* **Componentes**: Shunts de $1\ \Omega$ e $100\ \Omega$, amplificador LT1013 (ganho $\approx 31.3\times$), DACs MCP4725 (0x60 e 0x61), ADC ADS1115 (0x48) e transistor bleeder 2N3904.

### Camada 2: Abstração de Hardware (HAL)
* **Responsabilidade**: Isolar o hardware físico do software lógico.
* **Arquivos**: [hardware_hal.h](/home/luska/Documents/projects/esp32_mosfet_analysis/include/hardware_hal.h) e [hardware_hal.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/hardware_hal.cpp).
* **Lógica**: Comunicação I2C, leitura do ADC com oversampling de 16x a 64x e ordenação por Insertion Sort (stack-only), controle dinâmico de ganho (PGA), aplicação do modelo de calibração linear da porta de dreno ($V_d$), auto-range dinâmico de shunt e interface abstrata de fallback para hardware interno.

### Camada 3: Controle e Matemática
* **Responsabilidade**: Garantir estabilidade de polarização e calcular curvas analíticas.
* **Arquivos**: [mosfet_controller.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/mosfet_controller.cpp) e [math_engine.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/math_engine.cpp).
* **Lógica**: Algoritmo de malha fechada de duplo canal com tolerância de 2 mV e máximo de 10 iterações (compensação de degeneração de source). Processamento matemático de $G_m$ via derivadas suavizadas por Savitzky-Golay, cálculo de $V_{th}$ via extrapolação linear no ponto de $G_{m,max}$ e cálculo de $SS$ via regressão linear por janela deslizante no plano logarítmico.

### Camada 4: Orquestração e Sistema Operacional
* **Responsabilidade**: Gerenciamento de tarefas concorrentes e gravação de arquivos de dados.
* **Arquivos**: [main.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/main.cpp), [wifi_manager.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/wifi_manager.cpp), [file_manager.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/file_manager.cpp), [log_buffer.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/log_buffer.cpp).
* **Lógica**: Inicialização do ESP32, agendamento de tarefas do FreeRTOS (Core 0 para rede, Core 1 para varreduras), proteção do barramento I2C por mutex semafórico, buffer circular de logs gerenciado fisicamente por jumper no GPIO12, inicialização e gravação sequencial direta (sem alocação em heap) de CSVs na partição flash `FFat`.

### Camada 5: Transmissão e API
* **Responsabilidade**: Exposição de endpoints HTTP e envio de relatórios.
* **Arquivos**: [web_ui.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web_ui.cpp) e [email_manager.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/email_manager.cpp).
* **Lógica**: Roteamento do `ESPAsyncWebServer`, tratamento e serialização de requisições JSON da API REST, envio de arquivos CSV via SMTP de forma não-bloqueante utilizando conexões seguras.

### Camada 6: Apresentação (Cabeça)
* **Responsabilidade**: Interface de interação direta com o operador.
* **Arquivos**: Pasta [src/web/](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web/).
* **Lógica**: Interface gráfica no navegador, validação preliminar de inputs, polling de progresso de varredura, renderização gráfica das curvas utilizando Plotly.js e download ou disparo de exportação do CSV gerado.

---

## 3. Padrões de Projeto e Convenções de Software

### 3.1 Padrões de Software (Design Patterns)
* **HAL baseada em Estratégia (Strategy HAL)**: Uso da classe `IVoltageSource` como abstração para alternar em tempo de execução entre periféricos externos (MCP4725/ADS1115) e internos (DAC/ADC do ESP32) para facilitação de testes e robustez de hardware.
* **Model-View-Controller Simplificado (MVC)**: O CSV em `FFat` representa o Modelo; o firmware e a API REST representam o Controlador; a WebUI em JavaScript/HTML representa a Visualização.
* **Máquina de Estados de Varredura**: O `MOSFETController` implementa uma máquina de estados estrita (`STANDBY`, `HW_CHECK`, `MEASURING`, `COMPLETE`, `ERROR`) com sinalização luminosa via padrões de piscada do LED nativo (`led_status`).

### 3.2 Convenções Técnicas
* **Namespaces Obrigatórios**: Separação clara de responsabilidades no código C++: `hal::`, `mosfet::`, `webui::`, `math_engine::`.
* **Sem Console Log**: Proibição estrita de `console.log` no JavaScript. Utiliza-se a função customizada `dbg(categoria, mensagem)` para manter logs estruturados no browser (Categorias: `API`, `CSV`, `PLOT`, `UI`, `MATH`).
* **Debugging Físico**: Logs do ESP32 filtrados dinamicamente em runtime através da leitura do estado físico do pino GPIO12.
* **PROGMEM Web Assets**: Arquivos do frontend minificados e convertidos em vetores byte array estáticos salvos na memória Flash (`PROGMEM`) através de hook de pré-compilação em Python (`scripts/embed_web.py`).

---

## 4. Fluxo de Comunicação e Dependências Críticas

### 4.1 Sequência de Aquisição
```
[WebUI] ─── POST /api/start (JSON Config) ───► [Core 0: AsyncServer]
                                                    │ (IPC Queue)
                                                    ▼
[Core 1: MOSFETController] ◄── [FreeRTOS measurementTask] ◄─ (Wake)
         │
         ├──► Adquire Mutex I2C
         ├──► Executa calibração iterativa nos DACs (VGS e VDS)
         ├──► Lê canais do ADS1115 (A0-A3) com Oversampling e Trimmed Mean
         ├──► Libera Mutex I2C
         │
         ├──► MathEngine: Filtro Savitzky-Golay e diferenciação
         ├──► Escreve ponto de medição em buffer de FFat
         │
         └──► Atualiza status volátil (para polling do Core 0 /api/progress)
```

### 4.2 Dependências Críticas
* **Hardware Externo**: MCP4725 (DAC) e ADS1115 (ADC) comunicando via barramento I2C em $400\ \text{kHz}$. A falha de qualquer um desabilita a precisão analógica.
* **FreeRTOS Mutex**: Sem o lock correto do semáforo no barramento I2C, requisições paralelas feitas pelo servidor web corrompem as leituras de calibração ativa no loop de sweep.
* **Velocidade do Barramento I2C**: Dependência crítica em relação ao clock do barramento ($400\ \text{kHz}$). Se reduzido para $100\ \text{kHz}$, a latência do loop de calibração em malha fechada inviabiliza as varreduras rápidas.

---

## 5. Riscos Técnicos e Acoplamentos Importantes

* **Desgaste Prematuro da Flash (FFat)**: Como os pontos são gravados sequencialmente em arquivo na flash do ESP32 para economizar RAM, varreduras excessivamente longas e recorrentes podem desgastar os blocos de memória flash do microcontrolador.
  * *Mitigação*: Implementação de buffer de escrita de 2 KB em RAM, reduzindo a quantidade de ciclos físicos de escrita (flushes a cada 50 linhas gravadas).
* **Charge Injection no Switched-Capacitor MUX do ADS1115**: A comutação rápida entre os canais do ADC com tensões distantes (ex: canal A1 a 4 V e canal A3 a poucos microvolts) acopla cargas capacitivas internas, gerando erros sistemáticos de crosstalk na leitura do shunt amplificado.
  * *Mitigação*: Lógica de amortecimento em hardware (decoupling caps) e tempos de acomodação (settling) configuráveis em firmware.
* **Acoplamento Térmico no Shunt**: Altas correntes no transistor sob teste geram calor no resistor de shunt de $1\ \Omega$, alterando sua resistência e degradando a acurácia das correntes calculadas.
  * *Mitigação*: Limitador rígido por software em 500 mA e desligamento automático dos DACs em standby.

---

## 6. Diretrizes para Futuras Implementações

1. **Alteração em Sensores ou CIs de Leitura**: Deve-se iniciar adicionando a estrutura física no esquemático, atualizar o mapeamento de classes no HAL em `hardware_hal.h/.cpp`, garantir que a nova lógica atenda à interface genérica, ajustar a tolerância de erro de estabilização em `mosfet_controller.h`, e expandir a tela de hardware check no JS no frontend.
2. **Novos Algoritmos de Ajuste de Curva (Curve Fitting)**: Qualquer nova extração matemática (ex: resistência ôhmica em canal $R_{ds}$) deve ser implementada no `math_engine.cpp` de forma unitária, exportada como metadado anotado por comentários `#` no CSV de FFat, e mapeada pelo parser do frontend para plotagem de novos eixos gráficos em Plotly.js.
