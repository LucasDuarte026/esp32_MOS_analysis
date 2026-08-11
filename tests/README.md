# Módulo: Testes de Validação e Calibração (tests/)

> **Documentação de Referência - Spec-Driven Development (SDD)**  
> **Hierarquia:** Nível 3 (Validação e Testes de Lógica de Controle e Hardware)  
> **Status:** Ativo  

---

## 1. Objetivo do Módulo
Centralizar os programas de teste, scripts de simulação de malha e ensaios manuais de caracterização que validam a estabilidade térmica, resposta ao degrau dos DACs e a integridade matemática do firmware sem a necessidade de acoplar o sistema completo.

---

## 2. Responsabilidade Principal
Garantir que os loops de realimentação (controle PID), a excursão de tensão dos conversores (MCP4725/ADS1115) e o gerador de gráficos operem conforme as especificações físicas de projeto (erros $< 2\ \text{mV}$), fornecendo rotinas de teste isoladas para depuração de novos hardwares.

---

## 3. Funcionalidades Existentes (Suítes de Teste)

O módulo é dividido em subpastas de ensaios específicos:

* **`dac_manual_test/`**: Código simples para setar valores de tensão estáticos nos DACs via comando serial. Utilizado para calibração inicial de dois pontos com multímetro de bancada.
* **`dac_max_test/`**: Varre os DACs de 0 a 4095 (limites de 12 bits) em alta velocidade para avaliar a taxa de excursão máxima, linearidade de saída sob carga resistiva de dreno e limites de corrente.
* **`control_loop_test/`**: Simula ou executa loops de ajuste interativo de $V_{GS}$ e $V_{DS}$. Mede o tempo de convergência da malha e a taxa de overshoot nas transições sob efeito de degeneração de source.
* **`test_standalone/`**: Suíte de compilação local (ou firmware simplificado) para execução de lógicas lógicas do microcontrolador sem depender de Wi-Fi ou FFat.
* **`graph_maker/`**: Scripts secundários para leitura e conversão rápida de logs brutos em gráficos visuais rápidos durante a fase de prototipagem rápida.

---

## 4. Dependências Internas e Externas
* **Dependências Internas**:
  * [src/hardware_hal.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/hardware_hal.cpp) (utiliza as chamadas de baixo nível para controle dos pinos).
  * [include/mosfet_controller.h](/home/luska/Documents/projects/esp32_mosfet_analysis/include/mosfet_controller.h) (consome os limites operacionais de sweep).
* **Dependências Externas**:
  * **PlatformIO Native Test runner** (para testes standalone).
  * **Serial Monitor (115200 baud)**: Utilizado para interfaceamento e comandos manuais de teste.

---

## 5. Módulos Relacionados
* [src/](/home/luska/Documents/projects/esp32_mosfet_analysis/src/): Consome os dados e códigos validados nesta pasta para incorporá-los de forma estável no firmware de produção.
* [include/](/home/luska/Documents/projects/esp32_mosfet_analysis/include/): Mapeia as configurações estáticas avaliadas por estes testes.

---

## 6. Fluxos Importantes

### 6.1 Teste de Calibração Manual do DAC
Para validar a precisão da fonte de dreno e calcular os coeficientes de ganho da placa:
1. Grave o firmware contido em `dac_manual_test/` no ESP32.
2. Abra o Monitor Serial a `115200` baud.
3. Digite o valor de tensão desejado (ex: `1.5` Volts).
4. Meça com um multímetro de bancada de alta precisão no terminal do dreno.
5. Anote a leitura real e a leitura lida no canal A1 do ADS1115 para atualizar a calibração de dois pontos de $V_d$ em `include/hardware_hal.h`.

---

## 7. Observações Técnicas e Débitos Identificados

* **⚠️ Débito Técnico - Integração com Unity**: Os testes contidos em `tests/` são, em sua maioria, esboços de firmwares auxiliares (scripts de "rascunho") que exigem recompilação e upload manual. O projeto carece de uma suíte de testes unitários automatizada usando o framework Unity (nativo do PlatformIO) para validar a HAL e o motor matemático (`math_engine.cpp`) de forma automatizada no CI/CD.
* **⚠️ Débito Técnico - Simulação de MOSFET no Loop de Controle**: A validação da estabilidade do loop de controle em malha fechada exige a presença de um MOSFET físico conectado. Recomenda-se implementar uma lógica de simulação em software (DUT virtual usando modelo simplificado de Shockley) no controlador de testes, permitindo simular a convergência do loop fechado inteiramente dentro do microcontrolador sem conexões físicas de componentes.
