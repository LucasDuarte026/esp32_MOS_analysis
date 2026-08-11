# Documentação Mestre — Plataforma de Baixo Custo para Caracterização de MOSFETs

> **Versão:** 10.0.0 (Lançamento com Documentação Vasta)
> **Projeto:** PIBIC — Escola de Engenharia de São Carlos (EESC-USP)
> **Autor:** Lucas Sales Duarte (NUSP 11734490)
> **Orientadora:** Profa. Vanessa Cristina Pereira da Silva Venuto
> **Data:** Março/2026

---

## Descrição do Documento
Este arquivo constitui a base de conhecimento central do projeto, consolidando todas as decisões de hardware, arquitetura de firmware, estratégias de calibração e manuais de operação. Ele serve como o "Single Source of Truth" para o desenvolvimento técnico e a futura publicação científica, detalhando como uma plataforma de baixo custo (~R$ 200) consegue atingir precisão metrológica comparável a equipamentos de bancada de altíssimo custo através de complexidade interna e algoritmos de malha fechada.

## Índice

- [1. Visão Geral e Proposta Central](#1-visão-geral-e-proposta-central)
- [2. Contexto e Motivação](#2-contexto-e-motivação)
- [3. Decisões de Hardware](#3-decisões-de-hardware)
- [4. Os Três Modos de Operação Principal](#4-os-três-modos-de-operação-principal)
- [5. Decisões de Software e Arquitetura do Firmware](#5-decisões-de-software-e-arquitetura-do-firmware)
- [6. A Interface Web — Design para Simplicidade](#6-a-interface-web--design-para-simplicidade)
- [7. Dispositivos Sob Teste e Resultados](#7-dispositivos-sob-teste-e-resultados)
- [8. Arquitetura de Código — Níveis e Fluxogramas](#8-arquitetura-de-código--níveis-e-fluxogramas)
- [9. Proposta Central — Complexidade Interna → Simplicidade Externa](#9-proposta-central--complexidade-interna--simplicidade-externa)

---

## 1. Visão Geral e Proposta Central

### O Paradoxo do Projeto

A proposta central desta plataforma pode ser resumida em uma afirmativa aparentemente contraditória:

> **Hardware e firmware de elevada complexidade técnica → Plataforma de operação trivialmente simples.**

Um analisador de parâmetros de semicondutores SMU (*Source Measure Unit*) da Keysight, modelo B2902C, custa aproximadamente **R$ 85.000,00** (US$ 14.884,00 em 2026) e oferece resolução de 100 fA em corrente e 100 nV em tensão. A plataforma desenvolvida neste projeto realiza o mesmo escopo funcional básico com menor precisão, da ordem de ${100 \mu A}$ e ${10 \mu V}$ — extração de curvas I×V e parâmetros elétricos de MOSFETs — na faixa de **R$ 200,00**, uma redução superior a **99%** no custo.

Essa redução não é simplesmente uma troca de precisão por preço. É o resultado de decisões técnicas minuciosas que, em conjunto, criam um instrumento capaz de capturar fenômenos físicos delicados assim como a SMU, mas com menos precisão — como a corrente de difusão subthreshold de um MOSFET, na ordem de nanoampères — usando componentes disponíveis em qualquer loja de eletrônica se tornando uma solução não só barata como acessível.

A segunda dimensão do projeto é a **acessibilidade intelectual**: toda a complexidade interna é invisível ao operador. Para usar a plataforma basta conectar o MOSFET rede wifi local, abrir um navegador e clicar em "Iniciar". Não há instalação de software, configuração de drivers, familiaridade com SCPI ou planilhas manuais.

---

## 2. Contexto e Motivação

### 2.1 A Barreira Financeira

Equipamentos SMU comerciais tipicamente superam R$ 100.000,00 de custo de aquisição, tornando a prática experimental com MOSFETs economicamente inviável para a maioria dos laboratórios didáticos e grupos de pesquisa com recursos limitados — especialmente no contexto universitário brasileiro. O Keysight B2902C, por exemplo, usado como referência comparativa neste projeto, tem preço de catálogo de ~US$ 14.884,00.

### 2.2 A Barreira de Acesso Intelectual

Mesmo quando o equipamento está disponível, seu uso exige domínio de software proprietário (Keysight Interactive Developer) e procedimentos de calibração específicos, para seu MOSFET analisado. Isso cria uma segunda barreira que exclui não especialistas em microeletrônica e fora do nicho de metrologia e microeletrônica.

### 2.3 Estado da Arte em Plataformas de Baixo Custo

Iniciativas similares na literatura incluem:
- **Morsink (2021):** Traçador de curvas baseado em ATmega8 — tecnicamente relevante, mas requer software na estação de trabalho e não extrai parâmetros automaticamente.
- **Gomez-Casado & Perez-Lopez (2021):** Plataforma com PIC24FJ para ensino a distância — sem interface web autossuficiente nem cálculo embarcado de Vth, Gm e SS.

O diferencial desta plataforma em relação à literatura é a combinação de: (a) ausência total de software instalado no cliente; (b) interface web hospedada no próprio microcontrolador; (c) extração e visualização automática de parâmetros (Vth, Gm, SS); e (d) suporte a múltiplos modos de caracterização com controle em malha fechada por software.

---

## 3. Decisões de Hardware

### 3.1 Microcontrolador Principal: ESP32 Wroom 32D

**Problema:** O projeto foi originalmente planejado com PSoC 4 (Cypress). A migração para o ESP32 foi uma decisão técnica deliberada, motivada pela necessidade de Wi-Fi nativo para hospedar o servidor web diretamente no dispositivo, pelo custo similar, responsividade, acessibilidade e versatilidade.

| Característica | ATmega328 | STM32F4 | PSoC 4 | **ESP32 Wroom 32D** |
|---|---|---|---|---|
| Wi-Fi nativo | ❌ | ❌ | ❌ | ✅ |
| DAC nativo | ❌ | ✅ 12-bit | ✅ | ✅ 2 ch × 8-bit |
| Dual-core | ❌ | ❌ | ❌ | ✅ Xtensa LX6 |
| Custo (~R$) | 15 | 35 | 40 | **30** |

**Especificações relevantes:**
- CPU: Xtensa dual-core LX6, até 240 MHz
- RAM: 520 KB SRAM; Flash: 4 MB
- 2 DACs internos (8-bit) nos GPIO25 e GPIO26
- ADC interno (12-bit) — usado como fallback
- Wi-Fi 802.11 b/g/n integrado

**Decisão de uso dual-core:** O servidor HTTP (`ESPAsyncWebServer`) roda no Core 0. A tarefa de medição roda no Core 1. Isso garante que requests HTTP sejam respondidos em <50 ms mesmo durante varreduras longas (que podem durar vários minutos), sem bloqueio mútuo.

---

### 3.2 DAC Externo: MCP4725 (12-bit, I²C)

**Problema:** O DAC interno do ESP32 tem 8 bits → 256 níveis em 3,3V = **12,9 mV/passo**. Para capturar a transição de Ids em torno de Vth na região subthreshold, onde Ids varia em décadas por centenas de milivolts, essa resolução é inadequada.

**Solução:** MCP4725 — DAC de 12 bits com comunicação I²C.

**Cálculo de resolução:**
```
Com Vref = 5,0 V:  5,0 / 4096 ≈ 1,22 mV/passo  ← limiar prático adotado
Com Vref = 3,3 V:  3,3 / 4096 ≈ 0,81 mV/passo  ← resolução teórica máxima
```

O código adota **1,22 mV como limiar de alta confiabilidade metrológica** — patamar a partir do qual a relação sinal-ruído garante leituras consistentes e reprodutíveis com os MOSFETs testados.

**Dois módulos instalados:**
- Endereço **0x60** (pino ADDR → GND): controla **VGS** (tensão de porta)
- Endereço **0x61** (pino ADDR → VCC): controla **VDS** (tensão de dreno)

**Decisão operacional:** Escrita direta no registrador de saída via Fast Mode I²C (400 kHz), sem acionar a EEPROM interna do MCP4725. Isso reduz a latência de cada escrita de ~5 ms (com EEPROM) para <0,5 ms.

### 3.3 ADC Externo: ADS1115 (16-bit, I²C)

**Problema:** O ADC interno do ESP32 apresenta não-linearidade significativa acima de ~3,1 V e ruído elevado (~2–5 mV RMS), inadequado para leituras de shunts com quedas de poucos mV.

**Solução:** ADS1115 — ADC de 16 bits com PGA interno configurável.

**Auto-Range de Ganho (PGA):**
O firmware é projetado para trocar automaticamente o ganho do PGA do ADS1115 por canal. Com faixa de 0–5V com 16 bits de resolução, cobre-se uma faixa dinâmica de leitura de:
- **7,8 µV/bit** (ganho 16×, FSR ±0,256 V) → para correntes baixas no canal A3 amplificado.
- **187,5 µV/bit** (ganho 2/3×, FSR ±6,144 V) → para correntes altas no canal A0 direto.

**Mapeamento dos canais (Ordem Lógica: Porta → Dreno → Corrente):**

| Canal | Sinal | Função |
|---|---|---|
| **A2** | Tensão na porta (VG) | Realimentação fechada para calibração VGS |
| **A1** | Tensão no dreno (VD) | Realimentação fechada para calibração VDS |
| **A0** | Shunt direto (1 Ω ou 100 Ω) | Leitura de Ids — escala ampla, backup |
| **A3** | Shunt (100Ω)  amplificado ×31,3 | Leitura de Ids — alta precisão / sublimiar |

---

### 3.4 Amplificador de Condicionamento: LT1013

**Finalidade:** Amplificar a queda de tensão no shunt de 100 Ω (Modo 1) para que ela fique na faixa ótima do ADS1115 em ganho máximo. O **LT1013** foi escolhido para esta função (assim como para o buffer de VD) por sua alta precisão.

**Decisão de ganho:** O ganho foi determinado experimentalmente por medição direta da placa montada. Valor calibrado: **31,303951368×** (constante `SHUNT_AMP_GAIN` no firmware).

**Saturação e Auto-Range:**
O LT1013 alimentado a 5 V permite uma excelente excursão de sinal. O firmware monitora continuamente a tensão bruta no canal A3. Quando `raw_a3 ≥ 5,0 V`, o sistema **troca automaticamente para o canal A0** (shunt direto, sem amplificação) para continuar a medição sem saturação ou corte de sinal. Esta transição é **transparente ao usuário**.

**Offset DC residual:**
Diferente de componentes genéricos, o **LT1013 apresenta um offset típico de apenas 150 µV**. Isso garante que a relação sinal-ruído seja mantida mesmo em correntes extremamente baixas. O firmware subtrai este offset mínimo para máxima precisão:


---

### 3.5 Escolha dos Resistores Shunt por Modo

A corrente de dreno Ids varia em mais de 6 ordens de grandeza ao longo dos três modos de operação — de nanoampères (sublimiar) a miliampères (saturação). Nenhum shunt único oferece resolução adequada em toda a faixa sem saturar o amplificador ou ter queda de tensão insignificante. Por isso, dois shunts são usados com seleção pelo usuário:

| Modo | Shunt | Corrente típica | Queda no shunt | Justificativa |
|---|---|---|---|---|
| **Modo 1** (sublimiar) | **100 Ω** | nA – µA | µV – mV | Alta queda para resolução; degeneração desprezível (VGS << Vth) |
| **Modo 2** (sat., Ids×Vgs) | **1 Ω** | 0,1 – 50 mA | 0,1 – 50 mV | Degeneração mínima; compensada em malha fechada |
| **Modo 3** (Ids×Vds) | **1 Ω** | 0,1 – 100 mA | 0,1 – 100 mV | Idem Modo 2; faixa maior no triodo |

#### 3.5.1 Faixa de Operação e Proteção por Sobrecorrente

O sistema foi dimensionado e validado para operar confortavelmente até 200 mA** de corrente de dreno, que corresponde ao regime típico dos MOSFETs de sinal testados (BS170, 2N7000, CD4007) dentro da janela de 0–5 V da plataforma.

**Limiar de proteção — 500 mA:**
Se durante uma varredura a corrente de dreno calculada superar **500 mA**, o firmware **cancela automaticamente a medição** em andamento, desliga os DACs (VGS e VDS → 0 V), ativa o bleeder (GPIO14 → HIGH) para descarregar o nó de dreno e exibe ao usuário uma mensagem de erro específica na interface web:



**Justificativa do limiar:**
Com shunt de 1 Ω e ADS1115 com FSR ±6,144 V (GAIN_TWOTHIRDS), a queda máxima legível é ~6 V — o que fisicamente não ocorre com esses dispositivos na faixa de 0–5 V. O limiar de 500 mA é, portanto, um piso conservador para indicar condição anormal (curto no DUT, shunt errado ou VGS excessivo), não uma limitação da eletrônica de leitura.

---


### 3.6 Fonte de Corrente Controlada para VD: Buffer LT1013

A tensão de dreno VD é gerada pelo MCP4725 @ 0x61, seguido por um buffer baseado no operacional **LT1013** (Linear Technology). O LT1013 foi escolhido por:
- Tensão de offset típica: **150 µV**
- Adequado para aplicações de instrumentação de precisão
- Mantém a tensão de dreno estável dentro de **±2 mV** do valor alvo — coerente com a resolução do MCP4725 (1,22 mV/passo) e com a tolerância de convergência do loop de calibração (`VDS_GLOBAL_ERROR = 0.002f`)

---

### 3.7 Bleeder Ativo: Transistor 2N3904 + GPIO14

**Problema identificado durante testes:** Após uma medição com corrente significativa no dreno, capacitâncias parasitas da protoboard/PCB retinham carga no nó de dreno. A tensão não retornava a 0 V imediatamente após o DAC ser zerado, causando leituras errôneas no início da varredura seguinte.

**Solução implementada:** Um transistor NPN **2N3904** com base controlada pelo GPIO14 do ESP32. Durante os estados `STANDBY` e `IDLE`, o transistor é saturado, descarregando ativamente o nó do dreno para GND. Durante a medição ativa, o transistor é bloqueado (GPIO14 = LOW) para isolar o nó e permitir que o DAC controle a tensão com precisão.

---

### 3.8 Alimentação e Partição do Sistema

| Subsistema | Tensão | Fonte |
|---|---|---|
| ESP32 DevKit | 3,3 V | Regulador onboard do DevKit |
| MCP4725 × 2 | 5,0 V (Vref) | USB direto |
| ADS1115 | 3,3 V – 5,0 V | USB direto (5V) |
| LT1013 | 5,0 V | USB direto |
| MOSFET DUT (dreno) | 0 – 5,0 V | DAC MCP4725 → buffer LT1013 |
| MOSFET DUT (porta) | 0 – 5,12 V| DAC MCP4725 → direto |

---

## 4. Os Três Modos de Operação Principal

Todos os modos operam dentro da faixa de **0 a 5 V** em VGS e VDS, viabilizada pelos dois MCP4725 operando com Vref = 5 V.

---

### 4.1 Modo 1 — Ids × Vgs: Região Subthreshold e Cálculo do SS

#### Objetivo Físico

Caracterizar a corrente de dreno Ids em função da tensão de porta VGS na região em que **VGS < Vth** (abaixo da tensão de limiar). Nessa região, não há canal de inversão formado, mas existe uma corrente de difusão de portadores minoritários — exponencialmente sensível a VGS. Essa relação permite extrair o **Subthreshold Swing (SS)**:

```
SS = dVGS / d(log₁₀ Ids)   [mV/déc]
```

O limite teórico para MOSFETs de Si em temperatura ambiente é **60 mV/déc** (derivado de kT·ln(10)/q ≈ 60 mV/déc a 300 K). Valores comerciais típicos ficam entre 70 e 200 mV/déc.

#### Por que Ids NÃO Sofre Degeneração Nessa Faixa

Na região subthreshold, Ids é da ordem de nA a µA. Com shunt de 100 Ω:

```
Para Ids = 100 nA → Vsh = 10 µV   (degeneração ≈ 0)
Para Ids = 10 µA  → Vsh = 1 mV    (degeneração < 0,1% de VGS)
```

A queda no shunt é desprezível em comparação com VGS (~0,5–2,5 V). O MOSFET "enxerga" essencialmente o VGS comandado pelo DAC, sem necessidade de compensação por degeneração.

#### Configuração Elétrica

- **Shunt:** 100 Ω (série com o Source)
- **Leitura de Ids:** Canal A3 (amplificado ×31,3) como primário; fallback para A0 quando `raw_a3 ≥ 5,12 V`
- **VGS varrido:** 0 V → ~3,5 V (englobando a transição subthreshold → acima de Vth)
- **VDS fixo:** 0,1 V a 0,5 V (mantém saturação profunda para que Ids ≈ f(VGS) apenas)

#### Parâmetro Extraído: SS

O cálculo de SS no firmware usa **regressão linear por janela deslizante** sobre o plano `log₁₀(Ids) × VGS`:

1. Suavização leve com média móvel de 3 pontos (reduz spikes do ADC)
2. Janelas de teste de 5 a 20 pontos
3. Filtros: (a) log(Ids) deve ser crescente net; (b) variação ≥ 0,5 décadas; (c) slope ≥ 1 déc/V → SS ≤ 1000 mV/déc
4. Regressão linear → `slope` e `R²`
5. Aceita janela com melhor R² ≥ 0,85
6. `SS = (1/slope) × 1000` [mV/déc]; validação: 60 ≤ SS ≤ 1000

#### Problemáticas e Desafios

| Problema | Impacto | Solução |
|---|---|---|
| Ruído de quantização do ADC | Spikes no plano log(Ids)×VGS | Oversampling 16–64× + Trimmed Mean |
| Offset do LT1013 (150 µV) | Erro mínimo em Ids para correntes baixas | Subtração em firmware: `v -= 0.00015f` |
| Settling time longo | Leituras instáveis após troca de VGS | `settling_ms` configurável pelo usuário na UI (padrão: 0 ms; rec.: 0–5 ms) |
| Auto-range ADC → descontinuidade | "Salto" na curva ao trocar de ganho | Log suaviza descontinuidades; A3/A0 equivalentes após correção |

---

### 4.2 Modo 2 — Ids × Vgs em Saturação: Extração de Vth e Gm

#### Objetivo Físico

Medir Ids como função de VGS para **VGS > Vth**, na região de saturação. A curva tem perfil parabólico (lei de Shockley):

```
Ids = (1/2) · µn · Cox · (W/L) · (VGS − Vth)²
```

Isso permite extrair:
- **Tensão de limiar Vth** pelo método da extrapolação linear de √Ids × VGS
- **Transcondutância Gm** pela derivada numérica dIds/dVGS

#### Degeneração de Source e Compensação em Malha Fechada

Com shunt de 1 Ω, a queda Vsh = Ids × 1 Ω não é desprezível:

```
VGS_real = VG_aplicado − Vsh
VDS_real = VD_aplicado − Vsh
```

O firmware implementa **calibração em malha fechada (closed-loop)** para cada ponto de medição:

1. DAC aplica probe = target + Vsh_atual (melhor estimativa inicial)
2. ADC lê VG_real (A2) e Vsh (A0 ou A3)
3. Calcula VGS_medido = VG_lido − Vsh
4. erro = VGS_alvo − VGS_medido
5. Se |erro| > 2 mV: probe += erro; repete (máx. 10 iterações)
6. Mesma lógica para VDS via A1 e A0/A3

Esse loop garante que o MOSFET seja polarizado com os valores corretos, compensando a degeneração em tempo real — não por cálculo post-hoc.

#### Loop Duplo de Verificação Cruzada

Após calibrar VGS, a corrente muda → Vsh muda → VDS_real oscila. O firmware verifica e corrige:

```
Após calibrar VGS:
  → verifica VDS: se |vds_now − vds_alvo| > 2 mV → recalibra VDS
  → verifica VGS novamente: se |vgs_now − vgs_alvo| > 2 mV → recalibra VGS
```

#### Parâmetros Extraídos

**Vth — Extrapolação linear de √Ids × VGS:**
No ponto de máxima Gm, usando a relação Ids = Gm·(VGS − Vth):
```
Vth = VGS_maxGm − Ids_maxGm / Gm_max
```

**Gm — Diferença central com suavização Savitzky-Golay:**
```
Gm[i] = (Ids[i+1] − Ids[i-1]) / (VGS[i+1] − VGS[i-1])
```
Aplicada sobre Ids suavizado com filtro S-G (janela=5, ordem=2, coeficientes: [-3, 12, 17, 12, -3]/35).

---

### 4.3 Modo 3 — Ids × Vds: Curva de Saída (Triodo → Saturação)

#### Objetivo Físico

Medir Ids como função de VDS para VGS fixo (acima de Vth), capturando as regiões:

```
Triodo:     VGS > Vth  e  VDS < (VGS − Vth)   → Ids ∝ VDS (approx.)
Saturação:  VGS > Vth  e  VDS ≥ (VGS − Vth)   → Ids ≈ constante
```

O "joelho" da curva — ponto de transição — ocorre em `VDS = VGS − Vth`. Por exemplo, com VGS = 3 V e Vth ≈ 1,5 V, o joelho está em ~1,5 V.

#### Configuração Elétrica

- **VGS fixo:** 2,5 V ou 3,0 V (ajustável; garante transistor ligado para qualquer DUT com Vth < 2,5 V)
- **VDS varrido:** 0 V → 5 V
- **Shunt:** 1 Ω

**Família de curvas:** O outer loop fixa múltiplos valores de VGS; o inner loop varre VDS. Resultado: família de curvas Ids × VDS sobrepostas, análoga à saída de um SMU.

#### Estratégia de Calibração

- **VGS:** Calibrado uma única vez no início de cada curva com **3× o settling time** padrão (estabilidade máxima). Apenas verificação de deriva a cada passo interno.
- **VDS:** Recalibrado a cada passo do inner loop (VDS muda constantemente).

#### Problemáticas

| Problema | Impacto | Solução |
|---|---|---|
| Início triodo profundo (VDS → 0) | Ids muito baixo → A0 pode não resolver | A3 amplificado como primário; auto-range |
| Efeito Early (λ) | Ids não perfeitamente constante na saturação | Plataforma captura e a comparação com datasheet valida |
| Potência no DUT | P = Ids × VDS pode ser > 100 mW | Firmware limita VDS a 5 V; usuário deve respeitar ratings do DUT |

---

## 5. Decisões de Software e Arquitetura do Firmware

### 5.1 Linguagem, Framework e Ferramentas

- **Linguagem:** C++ (C++14), com namespaces obrigatórios: `hal::`, `mosfet::`, `webui::`, `math_engine::`
- **Framework:** Arduino API sobre ESP-IDF, gerenciado pelo PlatformIO
- **Servidor Web:** `ESPAsyncWebServer` — não-bloqueante, responde a requests mesmo durante varredura
- **Sistema de Arquivos:** FFat (FAT32 na partição de flash interna) — armazenamento de CSVs
- **RTOS:** FreeRTOS (nativo do ESP-IDF)
- **Versão atual:** `v9.3.1` (`include/version.h`)

### 5.2 Hardware Abstraction Layer (HAL)

O HAL (`hardware_hal.h/.cpp`) é o núcleo arquitetural que permite alternar entre periféricos internos e externos em tempo de execução, sem alteração no código de varredura.

**Dois modos:**
```
HW_INTERNAL:  DAC VDS → GPIO25 (8-bit)  |  DAC VGS → GPIO26 (8-bit)  |  ADC → GPIO34 (12-bit)
HW_EXTERNAL:  DAC VDS → MCP4725 @ 0x61 |  DAC VGS → MCP4725 @ 0x60  |  ADC → ADS1115 @ 0x48
```

**Interface abstrata:** `IVoltageSource` define `setVoltage()`, `getResolution()`, `shutdown()`. O `MOSFETController` chama apenas essa interface, sem saber qual periférico está ativo.

**Detecção em runtime:** `HardwareHAL::checkExternalDevices()` realiza probe I²C não-destrutivo nos endereços 0x60, 0x61 e 0x48. Se algum está ausente, a interface web exibe um modal descritivo, indicando exatamente qual componente está faltando, com botão para alternar para modo interno automaticamente.

### 5.3 Oversampling e Filtragem

#### Por que fazer oversampling?

O principal inimigo da medição em baixas correntes é o **ruído branco** — flutuações aleatórias e descorrelacionadas presentes em qualquer ADC real, causadas por:
- **Ruído térmico (Johnson-Nyquist):** gerado pela agitação térmica dos portadores de carga nas resistências internas do circuito analógico do ADC.
- **Ruído de quantização:** erro intrínseco de representação finita de uma grandeza contínua em N bits — seu espectro é aproximadamente plano (branco) para sinais que não são múltiplos exatos do LSB.

A propriedade fundamental do ruído branco que o oversampling explora é: **amostras independentes de ruído branco têm variância que decresce linearmente com N**. Tomando a média de N amostras, o desvio padrão do ruído cai por um fator de √N:

```
σ_N = σ_1 / √N
```

Isso equivale a ganhar `log₂(N)/2` bits de resolução efetiva (ENOB — Effective Number Of Bits). Para o ADS1115 de 16 bits com N=64: ENOB efetivo ≈ 19 bits.

**Condição de validade:** o oversampling simples (média pura) só recupera bits de resolução se o sinal de entrada tiver magnitude de ruído ≥ 0,5 LSB. O ADS1115 é um conversor sigma-delta que já realiza oversampling interno — o oversampling adicional no firmware atua sobre o ruído residual de quantização e ruído do barramento I²C, não sobre o ruído de conversão sigma-delta em si.

#### O que mais o oversampling resolve nesta plataforma?

Além do ruído branco, a técnica trata efeitos específicos do hardware:

| Efeito | Origem | Como o oversampling mitiga |
|---|---|---|
| **Glitches de barramento I²C** | Reflexões, capacitância, clock stretching | Outliers aparecem como amostras aberrantes isoladas — eliminados pelo Trimmed Mean |
| **Crosstalk de chaveamento DAC** | Transitório de VGS/VDS aparece no rail analógico momentaneamente | Amostras coletadas logo após o settling tendem a estar no extremo — descartadas pelo trim |
| **Dithering passivo** | Injeção natural de ruído térmico no LSB do ADS1115 | Permite que a média recover sub-LSB information (análogo ao dithering intencional de áudio) |
| **Spikes de commutação do ESP32** | WiFi/BT no Core 0 gera interferência EM periódica | Ocorrem em instantes discretos — mais prováveis de cair nos 10% descartados |

#### Algoritmo: Insertion Sort + Trimmed Mean (10% em cada extremo)

```
1. Coleta N amostras raw do ADC
2. Ordena por Insertion Sort (O(N²), stack-only, sem alocação heap — ideal para N ≤ 64)
3. Descarta bottom 10% e top 10% (remove outliers e spikes)
4. Média dos 80% centrais → tensão final
```

O Insertion Sort foi escolhido por ser **extremamente eficiente para arrays pequenos** (N ≤ 64) e operar inteiramente na stack do FreeRTOS, sem `malloc` — essencial num sistema sem heap fragmentável.

**Ganho de ENOB teórico:** `ENOB_gain ≈ log₂(N)/2` bits. Para N=16: +2 bits; para N=64: +3 bits.
Com ADS1115 de 16 bits e N=64: ENOB efetivo ≈ 19 bits no canal amplificado (A3).

**Dois modos de leitura:**
- `readVoltageFast()` → 2 amostras, sem filtragem — usado nos loops de calibração (prioridade: velocidade)
- `readVoltage()` → N amostras com Insertion Sort + Trimmed Mean — usado na leitura final do ponto (prioridade: precisão)


### 5.4 Auto-Range de PGA do ADS1115

| Código | Ganho | FSR | Resolução/bit |
|---|---|---|---|
| 0 | 2/3× | ±6,144 V | 187,5 µV |
| 1 | 1× | ±4,096 V | 125,0 µV |
| 2 | 2× | ±2,048 V | 62,5 µV |
| 4 | 4× | ±1,024 V | 31,25 µV |
| 8 | 8× | ±0,512 V | 15,6 µV |
| **16** | **16×** | **±0,256 V** | **7,8 µV** |

`ADC_GAIN_AUTO = 255` seleciona o ganho mais alto que não sature, **independentemente por canal**. O ganho escolhido é memorizado em `lastAutoGain_[4]` e reportado no CSV de saída para rastreabilidade metrológica.

### 5.5 Sistema de Logging por Hardware

O debug é controlado fisicamente pelo **GPIO12**:
- GPIO12 → GND: todos os níveis ativos (INFO, WARN, ERROR, DEBUG) — modo debug
- GPIO12 flutuante: apenas INFO e ERROR — modo produção

Nenhuma recompilação é necessária para ativar ou desativar logs. Em campo, um jumper ativa o debug completo. O buffer circular de logs (`LogBuffer`) é acessível via `/api/logs` da interface web.

### 5.6 Geração da Web UI — Embedding via PROGMEM

**Decisão:** O conteúdo web (HTML, CSS, JS) é embarcado na flash via PROGMEM. Não se usa SPIFFS/LittleFS para a UI.

**Fluxo de build:**
```
pre-build hook → scripts/embed_web.py → src/generated/web_dashboard.h (arrays PROGMEM)
                                       → servido via sendProgmemChunked()
```

**Vantagem:** A UI é completamente independente do sistema de arquivos FAT. A partição FFat (`/ffat`) é usada exclusivamente para os CSVs de medição, maximizando o espaço disponível para dados.

### 5.7 Multi-Core e Mutex I²C

```cpp
// Medição pinned ao Core 1
xTaskCreatePinnedToCore(
    measurementTaskWrapper,  // função
    "MOS_Measure",           // nome
    8192,                    // stack (8 KB — dimensionado para evitar overflow)
    this,                    // parâmetro
    1,                       // prioridade
    &taskHandle_,
    1                        // Core 1
);
```

Um `SemaphoreHandle_t` (mutex) protege todas as transações I²C. O mutex é adquirido antes de qualquer `ads.readADC_*()` ou `mcp.setVoltage()`, e liberado imediatamente após. Isso previne corrupção de dados em acessos simultâneos dos dois cores ao mesmo barramento.

### 5.8 Streaming Direto para FAT

**Decisão crítica de memória:** O ESP32 tem 520 KB de RAM. Uma varredura completa com 1000 pontos × múltiplas colunas float consumiria ~40 KB de RAM se acumulada em vetores. Para varreduras longas, isso esgotaria o heap.

**Solução:** Cada ponto é escrito diretamente no arquivo CSV em FFat via `currentFile_.printf()` imediatamente após a leitura. O buffer de escrita é de 2 KB. Flush a cada 50 linhas garante que dados não sejam perdidos em caso de reset. O único dado acumulado em RAM é o vetor da curva atual (para calcular Gm, Vth, SS ao final de cada curva do outer loop).

---

## 6. A Interface Web — Design para Simplicidade

### 6.1 Filosofia de Design

> "Qualquer estudante de engenharia elétrica deve conseguir realizar uma medição completa em menos de 5 minutos, sem consultar nenhuma documentação prévia."

Cada decisão de UX foi avaliada pelo critério: *isso reduz ou aumenta a carga cognitiva do operador?*

### 6.2 Arquitetura de Três Páginas

**Página 1 — Coleta (`/`):**
Responsabilidade única: configurar e disparar uma medição.
- Hardware check (probe I²C visual com indicadores ✅/❌ por componente)
- Configuração de parâmetros com valores padrão sensatos pré-preenchidos
- Seleção do modo de medição (Ids×Vgs / Ids×Vds) com descrição do que cada um faz
- Seleção de shunt (100 Ω ou 1 Ω) com dica de quando usar cada
- Barra de progresso em tempo real: "Medindo VGS = 2.350 V — 45% concluído"
- Botões Iniciar / Cancelar

**Página 2 — Visualização (`/visualization`):**
Responsabilidade única: analisar os dados coletados.
- Seleção de CSV armazenado no ESP32
- Lista de curvas por VDS/VGS populada automaticamente pelo parser do CSV
- Gráfico interativo multi-eixo (Plotly.js): Ids (escala log), Gm, sobreposição da reta tangente do SS
- Marcadores visuais de Vth e linha tangente do SS diretamente no gráfico
- Painel lateral: Vth, Gm máximo, SS (calculados no frontend a partir dos dados CSV)
- Download do CSV com um clique

**Página 3 — E-mail (`/email`):**
Responsabilidade única: exportar dados sem acesso ao sistema de arquivos do computador.
- Envio de CSVs por SMTP diretamente do ESP32
- Status de envio em tempo real via polling de `/api/email/status`

### 6.3 Módulos JavaScript

```
core.js          → utilitários, logging (dbg()), toasts (showToast()), constantes
collection.js    → start/cancel, polling /api/progress, lógica de HW check
visualization.js → Plotly, parser de CSV, cálculos (Vth, Gm, SS), exportação
email.js         → envio SMTP, status polling
```

**Regra inviolável:** `console.log` é proibido. Usar sempre `dbg('CATEGORIA', ...)`:
```javascript
dbg('API', 'Iniciando varredura com config:', config);
dbg('CSV', 'Linhas parseadas:', rows.length);
dbg('MATH', 'SS calculado:', ss_mVdec, 'mV/déc');
```

### 6.4 Sistema de Feedback Visual (Caixas de Erro e Guia)

**Modal de Hardware Ausente:** Se o probe I²C detectar componente faltando antes da medição:
```
┌─────────────────────────────────────────┐
│  ⚠️ Hardware Externo Incompleto         │
│                                         │
│  MCP4725 VGS (0x60)  ... ✅ OK          │
│  MCP4725 VDS (0x61)  ... ❌ NÃO FOUND   │
│  ADS1115    (0x48)   ... ✅ OK          │
│                                         │
│  [Verificar conexões]  [Usar Interno]   │
└─────────────────────────────────────────┘
```

**Toast por estado:** `showToast(msg, type)` com 4 tipos:
- 🟢 `success` — medição iniciada, arquivo salvo, e-mail enviado
- 🟡 `warning` — storage > 80%, settling time abaixo de 20 ms
- 🔴 `error` — hardware ausente, JSON inválido, storage cheio, VGS fora do range
- 🔵 `info` — calibração em andamento, arquivo baixado

**Validação em tempo real no frontend:** Limites de VGS (0–5 V) e VDS (0–5 V) são verificados antes do POST à API. Mensagens de erro específicas aparecem inline nos campos antes de qualquer request ao servidor.

### 6.5 Confiabilidade da Comunicação

**ESPAsyncWebServer:** Não-bloqueante por design. Requests HTTP são atendidos enquanto a tarefa de medição roda no Core 1, garantindo que o progresso e os logs sejam sempre acessíveis durante uma varredura ativa.

**Polling de progresso:** O frontend faz polling em `/api/progress` a cada 2 segundos. Payload JSON:
```json
{
  "running": true,
  "progress": 45,
  "vds": 2.350,
  "message": "Medindo VDS = 2.350V",
  "error": false,
  "error_msg": ""
}
```

**mDNS:** O ESP32 anuncia-se como `mosfet.local` via mDNS (ESP32mDNS). Elimina a necessidade de o usuário conhecer o IP do dispositivo:
```
http://mosfet.local/ → Página de Coleta
```

### 6.6 Manual de Uso Resumido — Fluxo Completo

```
1.  Conectar MOSFET ao circuito (Gate, Drain, Source nos terminais corretos)
2.  Selecionar shunt (100 Ω para Modo 1 subthreshold; 1 Ω para Modos 2 e 3)
3.  Ligar a alimentação USB 5 V
4.  Conectar ao Wi-Fi da plataforma ou à rede local configurada
5.  Abrir http://mosfet.local/ em qualquer navegador
6.  Selecionar o Modo de medição desejado
7.  Configurar faixa de VGS e VDS (valores padrão já sugeridos)
8.  Clicar em "Verificar Hardware" → aguardar ✅ em todos os componentes
9.  Clicar em "Iniciar Medição"
10. Aguardar progresso (sem intervenção — totalmente automatizado)
11. Ao concluir, navegar para /visualization
12. Selecionar o arquivo CSV gerado → curvas e parâmetros aparecem automaticamente
13. Analisar Vth, Gm e SS no painel lateral
14. Baixar CSV ou enviar por e-mail para análise complementar
```

**Tempos típicos de medição:**
- Modo 1 (sublimiar, 700 pontos, settling=150 ms): ~3–5 minutos
- Modo 2 (saturação, 500 pontos, settling=50 ms): ~1–3 minutos
- Modo 3 (Ids×Vds, 5 curvas × 100 pontos, settling=50 ms): ~2–4 minutos

---

## 7. Dispositivos Sob Teste e Resultados

### 7.1 Critério de Seleção

Os três dispositivos foram escolhidos para representar **perfis fundamentalmente distintos** de MOSFETs comerciais de silício do tipo N, todos operando confortavelmente na faixa de 0–5 V da plataforma, mas com características elétricas muito diferentes entre si.

| Parâmetro | **CD4007 (CMOS)** | **2N7000 (Switch)** | **BS170 (AF/RF)** |
|---|---|---|---|
| Finalidade do dispositivo | Digital CMOS | Chaveamento sinal | Amplificação áudio/RF |
| Encapsulamento | DIP-14 (CI) | TO-92 | TO-92 |
| Vth típico (datasheet) | 1,5 – 2,5 V | 0,8 – 3,0 V | 0,6 – 1,5 V |
| Ids máximo | ~10 mA | 200 mA | 500 mA |
| Rds(on) típico | — | 5 Ω | 5 Ω |
| Gm relativa esperada | baixa | média | alta |

### 7.2 Por que Esses Três Juntos?

**CD4007 (CI CMOS, porta NMOS):** Dispositivo de referência em qualquer laboratório de eletrônica. Por ser um processo CMOS de canal curto, espera-se SS próximo do limite teórico de 60 mV/déc e Vth bem definido. Serve como baseline de validação: se a plataforma acerta o CD4007 (amplamente documentado), ela é confiável.

**2N7000 (MOSFET de chaveamento):** Variabilidade de Vth propositalmente alta (especificado entre 0,8 V e 3,0 V no datasheet) — interessante para demonstrar que a plataforma captura cada dispositivo individualmente, não apenas médias. Bom para explorar a região de triodo (Ids×Vds) e Rds(on).

**BS170 (MOSFET de sinal AF/RF):** Vth baixo e Gm alta para pequeno sinal — perfil oposto ao 2N7000. Excelente para demonstrar que a plataforma distingue dispositivos de mesmo encapsulamento com finalidades distintas.

### 7.3 Resultados Esperados e Validação

As medições serão realizadas nos três dispositivos com os três modos de operação e os resultados serão comparados com:
1. **Datasheets dos fabricantes** (valores típicos de Vth, Gm, Ids × VGS)
2. **Medições no Keysight B2902C** (SMU de referência do laboratório EESC-USP)

**Hipótese de validação:** Espera-se que a plataforma extraia Vth com erro máximo de ±100 mV em relação ao SMU de referência, e SS com erro relativo máximo de ±15% — margens adequadas ao nível de precisão do instrumento e às incertezas inerentes à variabilidade de transistores comerciais.

**Demonstração da eficiência de captura:**
A distinção quantitativa entre os três dispositivos nas curvas extraídas — em especial a diferença de Vth, a inclinação de SS e a Gm máxima — demonstrará que a plataforma tem sensibilidade suficiente para caracterizar individualmente dispositivos de diferentes famílias tecnológicas.

**Dados a inserir (após conclusão das medições comparativas):**

| Parâmetro | CD4007 Plataforma | CD4007 SMU | 2N7000 Plataforma | 2N7000 SMU | BS170 Plataforma | BS170 SMU |
|---|---|---|---|---|---|---|
| Vth (V) | — | — | — | — | — | — |
| SS (mV/déc) | — | — | — | — | — | — |
| Gm máx (mS) | — | — | — | — | — | — |

---

## 8. Arquitetura de Código — Níveis e Fluxogramas

### 8.1 Nível 1 — Visão de Alto Nível do Sistema

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            ESP32 Wroom 32D                                  │
│                                                                             │
│   ┌────────────────────┐   Barramento I²C (400 kHz)   ┌─────────────────┐  │
│   │      CORE 0        │ ◄────────────────────────────►│ Periféricos I²C │  │
│   │  ─────────────     │                               │ ─────────────── │  │
│   │  AsyncWebServer    │                               │ MCP4725 @ 0x60  │  │
│   │  REST API          │                               │   (DAC VGS)     │  │
│   │  mDNS              │                               │ MCP4725 @ 0x61  │  │
│   │  Wi-Fi Stack       │                               │   (DAC VDS)     │  │
│   └────────┬───────────┘                               │ ADS1115 @ 0x48  │  │
│            │  FreeRTOS IPC                             │   (ADC 4 ch.)   │  │
│            │  (Mutex I²C + Volatile)                   └─────────────────┘  │
│   ┌────────▼───────────┐              ┌─────────────────────────────────┐   │
│   │      CORE 1        │              │      Circuito Analógico          │   │
│   │  ─────────────     │              │  ─────────────────────────────  │   │
│   │  MOSFETController  │──── GPIOs ──►│  LT1013 (ganho ×31.3 — A3)     │   │
│   │  Tarefa de Sweep   │              │  LT1013 (buffer VD)             │   │
│   │  HAL I²C           │              │  2N3904 (bleeder GPIO14)        │   │
│   │  MathEngine        │              │  Shunt 100 Ω / 1 Ω             │   │
│   └────────────────────┘              │  MOSFET DUT (Gate/Drain/Source) │   │
│                                       └─────────────────────────────────┘   │
│   ┌─────────────────────────────────────────────────────────────────────┐   │
│   │  Partição FFat (Flash interna)                                      │   │
│   │  /measurements/ → arquivos CSV das varreduras                       │   │
│   └─────────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

### 8.2 Nível 2 — Máquina de Estados do Sistema

```
              ┌──────────────┐
              │    BOOT      │
              │  (setup())   │
              │ inicia HAL,  │
              │ FFat, Wi-Fi, │
              │ servidor,    │
              │ LED, mDNS    │
              └──────┬───────┘
                     │
                     ▼
              ┌──────────────┐◄─────────────────────────────────┐
              │   STANDBY    │   (medição concluída ou cancelada) │
              │  LED: blink  │                                    │
              │  lento       │                                    │
              │  Bleeder ON  │                                    │
              └──────┬───────┘                                    │
                     │                                            │
              POST /api/start                                     │
              (configuração válida)                               │
                     │                                            │
                     ▼                                            │
              ┌──────────────┐                                    │
              │  HW CHECK    │──── falhou ────────────────────────┤
              │ probe I²C    │    (HTTP 424, modal na UI)         │
              │ 0x60,61,0x48 │                                    │
              └──────┬───────┘                                    │
                     │ OK                                         │
                     ▼                                            │
              ┌──────────────┐   cancelMeasurement()             │
              │  MEASURING   │──── cancelado ────────────────────►│
              │ Core 1 ativo │   (deleta CSV parcial)            │
              │ LED: blink   │                                    │
              │ rápido       │                                    │
              │ Bleeder OFF  │                                    │
              └──────┬───────┘                                    │
                     │ sweep completo                             │
                     ▼                                            │
              ┌──────────────┐                                    │
              │   COMPLETE   │                                    │
              │ flush CSV    │───────────────────────────────────►│
              │ fecha arquivo│
              │ shutdown DAC │
              └──────────────┘

              Em qualquer estado:
              ┌──────────────┐
              │    ERROR     │ ← HAL init fail, FFat cheio,
              │  LED: SOS    │   arquivo não pode ser aberto
              └──────────────┘
```

---

### 8.3 Nível 3 — Fluxo Interno da Tarefa `performSweep()`

```
performSweep() — Core 1
│
├─ Abre arquivo CSV em FFat
├─ Escreve cabeçalho completo (metadados + colunas)
│
│  ┌─────────────────────────────────────────────────────────────────────┐
│  │  MODO SWEEP_VGS  (Ids × Vgs) — padrão Modos 1 e 2                  │
│  │                                                                     │
│  │  Para cada VDS (outer loop):                                        │
│  │  │                                                                  │
│  │  │  calibrateVDS(vds, settling × 3):                                │
│  │  │    probe = vds + Vsh_fast                                        │
│  │  │    setVDS(probe); wait(settling)                                 │
│  │  │    vds_meas = readVD_fast() − readVsh_fast()                     │
│  │  │    while |erro| > 2 mV && iter < 10: probe += erro; repete      │
│  │  │                                                                  │
│  │  │  Para cada VGS (inner loop):                                     │
│  │  │  │                                                               │
│  │  │  │  calibrateVGS(vgs, settling) — mesmo algoritmo, erro > 2 mV │
│  │  │  │                                                               │
│  │  │  │  // Verificação cruzada de deriva                             │
│  │  │  │  Se |VDS_now − vds| > 10 mV → recalibra VDS                  │
│  │  │  │  Se |VGS_now − vgs| > 50 mV → recalibra VGS                  │
│  │  │  │                                                               │
│  │  │  │  // Leitura final (oversampled)                               │
│  │  │  │  vd_actual ← readVD_Actual()        [A1, oversampled]         │
│  │  │  │  vg_actual ← readVG_Actual()        [A2, oversampled]         │
│  │  │  │  sh = measureShuntSample():                                   │
│  │  │  │    raw_a3     ← readShuntAMPRawFast()  [A3, fast]             │
│  │  │  │    vsh_precise = raw_a3 / 31.304 − 0.072                      │
│  │  │  │    vsh_a0     ← readShuntVoltage()     [A0, oversampled]      │
│  │  │  │    vsh_for_ids = (raw_a3 < 5.12V) ? vsh_precise : vsh_a0     │
│  │  │  │                                                               │
│  │  │  │  ids       = vsh_for_ids / Rshunt                             │
│  │  │  │  vds_true  = vd_actual − vsh_for_ids                          │
│  │  │  │  vgs_true  = vg_actual − vsh_for_ids                          │
│  │  │  │                                                               │
│  │  │  │  Escreve linha CSV; flush a cada 50 linhas                    │
│  │  │  └──────────────────────────────────────────────────────────────┘
│  │  │
│  │  │  calculateCurveParams(curva atual):
│  │  │    Gm  ← calculateGm()  [Savitzky-Golay + diff. central]
│  │  │    Vth ← calculateVt()  [Peak Gm → extrapolação]
│  │  │    SS  ← calculateSS()  [sliding-window regression]
│  │  │  Escreve comentário CSV: # VDS=x: Vt_Gm=y, Vt_SS=w, SS=z, MaxGm=k
│  │  └─────────────────────────────────────────────────────────────────┘
│
│  ┌─────────────────────────────────────────────────────────────────────┐
│  │  MODO SWEEP_VDS  (Ids × Vds) — Modo 3                               │
│  │                                                                     │
│  │  Para cada VGS (outer loop):                                        │
│  │    calibrateVGS(vgs, settling × 3)  — uma vez por curva            │
│  │                                                                     │
│  │    Para cada VDS (inner loop):                                      │
│  │      calibrateVDS(vds, settling)    — a cada passo                  │
│  │      // Verifica deriva VGS; recalibra se necessário                │
│  │      // Verifica deriva VDS; recalibra se necessário                │
│  │      Leitura final → mesma sequência do SWEEP_VGS                  │
│  │      Escreve linha CSV                                              │
│  └─────────────────────────────────────────────────────────────────────┘
│
├─ flush final; shutdown DACs    (hal::shutdown())
└─ closeMeasurementFile(); LED → STANDBY
```

---

### 8.4 Nível 4 — Pipeline de Cálculo do Subthreshold Swing (SS)

```
Entrada: ids[], vgs_true[]
│
├─ [1] Suavização leve (média móvel 3 pt sobre ids)
│       → idsSmooth[]
│
├─ [2] Construir logIds[]
│       Para cada i: se idsSmooth[i] > 1e-13 A
│           logIds[i] = log₁₀(|idsSmooth[i]|)
│           usable[i] = true
│       senão: usable[i] = false
│
├─ [3] Sliding-window linear regression
│       Para janela win = 5 até 20 pontos:
│         Para cada início start no vetor:
│           Coleta (vgs[i], logIds[i]) para usable[i] == true
│           │
│           Filtro 1: logIds deve ser net-crescente
│           Filtro 2: Δlog(Ids) ≥ 0,5 décadas na janela
│           │
│           Regressão linear → slope (déc/V), intercept, R²
│           │
│           Filtro 3: slope ≥ 1,0 déc/V  (→ SS ≤ 1000 mV/déc)
│           │
│           Guarda melhor R²
│
├─ [4] Validação e resultado
│       R² ≥ 0,85  e  slope > 0?
│         SS = (1 / slope) × 1000  [mV/déc]
│         Cheque físico: 60 ≤ SS ≤ 1000
│         → SSResult válido com pontos da reta tangente (x1,y1,x2,y2)
│       Senão:
│         → SSResult.valid = false (sem SS plotado)
│
└─ Saída: SSResult { ss_mVdec, valid, x1, y1, x2, y2 }
          → reta tangente para overlay no dashboard
```

---

### 8.5 Nível 5 — Formato do CSV de Saída

```
# MOSFET Characterization Data
# Date: 2026-03-29 14:35:22
# Sweep Mode: VGS
# Rshunt: 100.000 Ohms
# VDS Range: 0.100 to 0.100 V (step 0.100)
# VGS Range: 0.000 to 3.500 V (step 0.005)
# Settling Time: 150 ms
# Oversampling: enabled (16x)
# ADC Gains: VSh=AUTO (Dynamic Optimize), VD=AUTO, VG=AUTO
# HW: Fully External (VDS: MCP4725@0x61, VGS: MCP4725@0x60, ADC: ADS1115@0x48)
# Firmware: 9.3.1
# Shunt: LT1013_gain=31.303951368 | A3_DC_offset=0.0002V | Switch threshold=5.12V | PGA: AUTO
#
timestamp,vd,vg,vd_read,vg_read,vsh,vsh_precise,vds_true,vgs_true,ids
1701234567,0.100,0.000,0.1018,0.0009,0.000001,0.000001,0.1018,0.0009,1.00e-08
1701234717,0.100,0.005,0.1019,0.0053,0.000001,0.000001,0.1019,0.0053,9.87e-09
...
1701269999,0.100,3.500,0.1021,3.5002,0.004512,0.004489,0.0976,3.4957,4.49e-05
# VDS=0.100V: Vt_Gm=1.523V, Vt_SS=1.490V, SS=87.45 mV/dec, MaxGm=2.34e-03 S, SS_Tangent_VGS:...
#   SS_Tangent_VGS:0.800,1.300  SS_Tangent_LogId:-9.200,-8.123
```

**Significado de cada coluna:**

| Coluna | Descrição |
|---|---|
| `timestamp` | `millis()` relativo ao boot do ESP32 |
| `vd`, `vg` | Tensão comandada ao DAC (alvo) |
| `vd_read`, `vg_read` | Tensão real medida nos nós (A1, A2 do ADS1115) |
| `vsh` | Queda no shunt via canal A0 (direto, oversampled) |
| `vsh_precise` | Queda no shunt via canal A3 (amplificado, corrigido pelo ganho e offset) |
| `vds_true` | VDS real = `vd_read − vsh_for_ids` |
| `vgs_true` | VGS real = `vg_read − vsh_for_ids` |
| `ids` | Corrente de dreno = `vsh_for_ids / Rshunt` |

As linhas de comentário `# VDS=...` ao final de cada curva carregam Vth, SS, Gm e os pontos da reta tangente do SS, utilizados diretamente pelo dashboard de visualização para o overlay do gráfico.

---

## 9. Proposta Central — Complexidade Interna → Simplicidade Externa

### 9.1 O que o Usuário NÃO VÊ

Por baixo de um simples botão "Iniciar Medição", o firmware executa:

| Complexidade interna | Onde está |
|---|---|
| Controle em malha fechada dupla (VDS e VGS diferencial) | `calibrateVDS()`, `calibrateVGS()` em `mosfet_controller.cpp` |
| Auto-range de PGA independente por canal (A0, A1, A2, A3) | `ExternalADC::readVoltage()` em `hardware_hal.cpp` |
| Compensação de offset DC do LT1013 (150 µV típico) | `shuntAmplifiedAdcToVoltage()` em `hardware_hal.h` |
| Troca automática de canal A3 → A0 por saturação do amplificador | `measureShuntSample()` em `hardware_hal.cpp` |
| Oversampling Insertion Sort + Trimmed Mean 80% | `ExternalADC::readVoltage()` |
| Suavização Savitzky-Golay para diferenciação numérica | `math_engine::savitzkyGolaySmooth()` |
| Regressão linear por janela deslizante para SS (R² ≥ 0,85) | `math_engine::calculateSS()` |
| Streaming direto para FAT (sem acumulação em RAM) | `performSweep()` |
| Multi-core com mutex I²C | `HardwareHAL::getI2CMutex()` |
| Bleeder ativo entre medições (2N3904 + GPIO14) | `led_status::setState()` |
| Detecção de periféricos I²C em runtime | `HardwareHAL::checkExternalDevices()` |
| Embedding da UI em PROGMEM (sem SPIFFS) | `scripts/embed_web.py` + `sendProgmemChunked()` |

### 9.2 O que o Usuário EXPERIMENTA

| Simplicidade externa | Onde está |
|---|---|
| Zero instalação de software | Wi-Fi + navegador qualquer |
| Acesso pelo url: (`esp32-mosfet.local`) no navegador em mesma rede wifi | mDNS (ESP32mDNS) |
| Hardware check visual com guia de solução | Modal com ✅/❌ por componente |
| Progresso contextual em tempo real | Polling `/api/progress` a cada 2 s |
| Parâmetros Vth, Gm, SS calculados automaticamente | Frontend + firmware em `calculateCurveParams()` |
| Reta tangente do SS sobrepostas no gráfico | `SSResult {x1,y1,x2,y2}` no CSV |
| Download com um clique ou e-mail direto | `/api/files/download` e `/api/email/send` |
| Interface responsiva (celular, tablet, desktop) | CSS responsivo sem framework externo |
| Manual de uso em 14 passos, sem jargão técnico | Seção 6.6 deste documento |

### 9.3 A Validação da Proposta

A eficácia da plataforma é validada pela sua capacidade de **distinguir e caracterizar quantitativamente** três MOSFETs comerciais de perfis distintos (CD4007, 2N7000, BS170) com resultados coerentes com os datasheets e com o SMU Keysight B2902C do laboratório.

| Critério de validação | Método |
|---|---|
| Acurácia de Vth | Comparação com datasheet + SMU; erro esperado ≤ ±100 mV |
| Acurácia de SS | Comparação com SMU; erro relativo esperado ≤ ±15% |
| Distinção entre dispositivos | Curvas claramente diferentes para CD4007, 2N7000 e BS170 |
| Rastreabilidade metrológica | CSV com todos os metadados (ganho, firmware, modo, offset) |
| Reprodutibilidade | Múltiplas medições do mesmo dispositivo com variação < 5% |

### 9.4 A Narrativa do Artigo

A tese que o artigo defende, sustentada por este projeto, é:

> *É possível construir uma plataforma de caracterização de MOSFETs que faça o que um SMU de R$ 200.000 faz — dentro do escopo de dispositivos de silício de baixa e média potência — por R$ 161,00, utilizando microcontroladores e conversores digitais disponíveis comercialmente, desde que as limitações de hardware sejam compensadas por decisões de firmware cuidadosas (malha fechada, auto-range, filtragem) e que a interface de uso seja projetada deliberadamente para eliminar as barreiras intelectuais de acesso ao instrumento.*

A combinação de:
- **Hardware de baixo custo** (ESP32 + MCP4725 + ADS1115 + componentes discretos)
- **Firmware de alta complexidade** (controle, filtragem, cálculo embarcado)
- **Interface de altíssima simplicidade** (zero configuração, zero instalação, zero curva de aprendizado)

...resulta em um instrumento que democratiza a caracterização experimental de MOSFETs para qualquer laboratório universitário, grupo de pesquisa independente ou educador que precise introduzir práticas de microeletrônica sem o ônus financeiro de equipamentos SMU comerciais.

---

*Documentação gerada em 29/03/2026 — firmware v9.3.1*
*Próxima atualização: inserção dos resultados quantitativos comparativos após conclusão das medições com o Keysight B2902C.*
