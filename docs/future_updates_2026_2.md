# Atualizações Futuras - Semestre 2 de 2026

Este documento foi criado para registrar problemas sistêmicos e arquiteturais identificados durante revisões, servindo de guia para correções nas próximas versões do firmware e hardware da plataforma ESP32 MOSFET Analysis.

---

## 1. Problema de Autoaquecimento (Self-Heating) / Degradação de Mobilidade

**Qual é o problema?**
As medições do dispositivo em regiões de maior potência apresentaram um erro de -16.11% na transcondutância máxima ($g_{m,max}$) para o 2N7000 e um alto erro (NRMSE ~32%) nas curvas de saída em saturação, se comparadas com um Source Measure Unit (SMU) Keysight. A causa desse erro não é o hardware analógico em si, mas sim a técnica de **varredura contínua** empregada pelo firmware. Como o componente fica energizado de forma ininterrupta ao longo de toda a coleta de dados, a temperatura da junção ($T_j$) aumenta por efeito Joule, o que diminui a mobilidade dos portadores de carga e gera uma leitura subestimada do $g_m$.

**Onde está o problema no código?**
A arquitetura temporal da medição se agrava devido a três fatores que alongam a permanência do transistor sob teste:
1. **Falta de Cooldown (Tempo de descanso):** No arquivo `src/mosfet_controller.cpp`, a função `performSweep()` é um laço `for` síncrono. Não há nenhum pulso ou tempo de descanso com $0V$ aplicado entre os pontos. O MOSFET fica ativado direto.
2. **Tempo do Oversampling:** O ADC externo ADS1115 via I2C gasta cerca de 1.16 ms por conversão bruta. Como o `oversampling` padrão (`SweepConfig`) é 16, a captura de 3 canais (VDS, VGS, VSH) demanda mais de `~56 ms` em leituras absolutas.
3. **Overhead de Calibração Closed-Loop:** O loop de calibração que precede cada medição (`calibrateVDS` e `calibrateVGS`) tenta realizar a aproximação em malha fechada e leva em torno de `10 a 20 ms` de overhead no pior cenário (múltiplos reads *Fast* de 2 amostras + delay `settling_ms`).

**Soma de tudo:** O MOSFET fica com a porta (Gate) aberta e conduzindo corrente pelo Dreno por **cerca de 75 a 85 milissegundos por ponto medido**. Uma curva com 200 pontos submete o transistor a um estresse térmico contínuo de **~16 segundos**. (Um equipamento da Keysight faz isso em *microssegundos* e logo apaga o pulso).

**O que e como mudar?**
Para mitigarmos esse desvio, o firmware precisará suportar um **Pulsed Sweep Mode** (Varredura Pulsada).
- **Abordagem via Firmware (Simples):** Alterar o `mosfet_controller.cpp` para que, ao final de cada iteração do loop e gravação do ponto (ou após um bloco de N pontos), o firmware chame `hal::setVGS(0.0f);` e `hal::setVDS(0.0f);` forçando as saídas a 0V, acompanhado de um `vTaskDelay(pdMS_TO_TICKS(X))` (ex: 50 a 100 ms) para permitir que a cápsula (TO-92) do MOSFET dissipe o calor ambiente, antes de subir a tensão para o próximo degrau.
- **Abordagem via Hardware (Avançada):** Adicionar no PCB um "Switch" físico (como um MOSFET secundário ultra-rápido do tipo chopper na base do Dreno) gerido pelo ESP32 via GPIO de hardware (PWM) para chavear/ligar o circuito de teste apenas nos exatos microsegundos em que o ADC realizará o sampling.

**Como isso será melhor para o projeto?**
Esta correção mitigará imediatamente as distorções em correntes maiores (saturação e triodo avançado). Se as medições em subthreshold e triodo leve já demonstraram erro de < 3%, com a contenção térmica, a curva de saída inteira reduzirá o erro drásticamente, equiparando sua extração de parâmetros ($g_{m}$ e resistência $R_{DS(on)}$ em saturação) aos SMUs Keysight com muita precisão, viabilizando uso científico contundente do projeto.

---

*(Novos problemas e tarefas de refatoração identificados no futuro serão inseridos nas seções abaixo).*
