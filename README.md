# ESP32 MOSFET Analysis Platform

> Plataforma embarcada de caracterização de MOSFETs com servidor web autossuficiente, controle em malha fechada e extração automática de parâmetros elétricos.
>
> **Firmware:** v10.0.0 · **Target:** ESP32 Wroom 32D · **Interface:** Browser (zero instalação)

---

## O que é

Um instrumento de bancada baseado em ESP32 que extrai curvas I×V e parâmetros elétricos de MOSFETs — Vth, Gm e Subthreshold Swing — diretamente no dispositivo, sem nenhum software instalado no computador. A interface é uma página web hospedada no próprio ESP32, acessível por qualquer navegador na rede local.

O projeto foi desenvolvido como pesquisa científica (PIBIC, EESC-USP) para demonstrar que é possível obter caracterização metrológica com hardware de ~R$ 200, desde que as limitações sejam compensadas por firmware cuidadoso.

---

## Modos de Operação

| Modo | Sweep | VDS/VGS Fixo | Shunt | Parâmetro Extraído |
|---|---|---|---|---|
| **1 — Subthreshold** | VGS: 0 → 3,5 V | VDS fixo (0,1 V) | 100 Ω | Subthreshold Swing (SS) |
| **2 — Saturação** | VGS: 0 → 5 V | VDS fixo | 1 Ω | Vth, Gm máx |
| **3 — Curva de Saída** | VDS: 0 → 5 V | VGS fixo (família) | 1 Ω | Curva Ids×Vds, região triodo/saturação |

---

## Hardware

```
ESP32 Wroom 32D  ─── I²C (400 kHz) ───  MCP4725 @ 0x60   (DAC 12-bit, VGS)
                                    ───  MCP4725 @ 0x61   (DAC 12-bit, VDS → buffer LT1013)
                                    ───  ADS1115 @ 0x48   (ADC 16-bit, 4 canais)

ADS1115 canais:
  A0 ← Shunt direto        (backup / alta corrente)
  A1 ← VD via resistor     (realimentação VDS)
  A2 ← VG via resistor     (realimentação VGS)
  A3 ← Shunt × 31,3 (LT1013)  (precisão em baixas correntes)

GPIO14 ── 2N3904 ── GND    (bleeder ativo: descarrega nó de dreno entre medições)
GPIO12 ── jumper GND       (ativa modo debug completo sem recompilação)
```

**Resolução:**
- DAC: 1,22 mV/passo (MCP4725, Vref = 5 V)
- ADC canal A3: ~7,8 µV/bit efetivo (ADS1115 ganho 16×, oversampling 64×, ENOB ≈ 19 bits)
- Corrente mínima detectável: ordem de nA (canal amplificado A3)
- Faixa operacional validada: 100 mA nominal, proteção automática em 500 mA

---

## Firmware — Decisões Técnicas Relevantes

**Controle em malha fechada:** Cada ponto de medição é calibrado iterativamente. O firmware aplica VGS/VDS alvo, lê os valores reais (A1, A2), calcula o erro e corrige o DAC até |erro| < 2 mV ou 10 iterações. Isso compensa a degeneração de source (queda no shunt) em tempo real.

**Dual shunt com auto-range:** O canal A3 (amplificado ×31,3 via LT1013) é usado como primário. Quando a tensão bruta em A3 ≥ 5,0 V (saturação do amplificador), o sistema troca automaticamente para A0. A transição é transparente na curva gerada.

**Oversampling + Trimmed Mean:** Cada leitura final coleta N amostras (até 64×), ordena por Insertion Sort (stack-only, sem heap), descarta os 10% extremos e retorna a média dos 80% centrais. Elimina spikes de I²C e interferência do Wi-Fi do Core 0.

**Multi-core + mutex I²C:** O servidor HTTP roda no Core 0; a tarefa de medição, no Core 1. Um semáforo protege todas as transações I²C. Requests HTTP são respondidos em < 50 ms durante varreduras ativas.

**Streaming direto para FAT:** Cada ponto é escrito no CSV em FFat imediatamente após a leitura, sem acumular em RAM. Flush a cada 50 linhas. O ESP32 tem 520 KB de RAM — essa decisão torna o tamanho da varredura ilimitado pela memória.

**UI embarcada em PROGMEM:** O HTML/CSS/JS é compilado pelo pre-build hook `scripts/embed_web.py` e servido via arrays PROGMEM. A partição FFat é usada exclusivamente para os CSVs de medição.

---

## Interface Web

**`/` — Coleta:** Configuração de parâmetros, verificação de hardware (probe I²C visual com ✅/❌ por componente), progresso em tempo real com valor de VGS/VDS atual.

**`/visualization` — Análise:** Seleção de CSV armazenado, gráfico interativo (Plotly.js) com Ids em log, Gm, reta tangente do SS e marcador de Vth. Parâmetros exibidos no painel lateral.

**`/email` — Exportação:** Envio de CSVs por SMTP diretamente do ESP32, sem passar pelo computador.

Acesso via mDNS: `http://mosfet.local/`

---

## Formato CSV de Saída

```
# MOSFET Characterization Data
# Firmware: 10.0.0 | HW: Fully External | Rshunt: 100 Ohms
# Shunt: LT1013_gain=31.303951368 | A3_DC_offset=0.000150V | Switch threshold=5.0V
#
timestamp,vd,vg,vd_read,vg_read,vsh,vsh_precise,vds_true,vgs_true,ids
...
# VDS=0.100V: Vt=1.523V, SS=87.45 mV/dec, MaxGm=2.34e-03 S
```

`vds_true` e `vgs_true` são as tensões reais nos terminais do MOSFET (com subtração da queda no shunt). O CSV contém os metadados completos de cada medição para rastreabilidade.

---

## Como Usar

```bash
# 1. Credenciais Wi-Fi
cp include/secrets.h.example include/secrets.h
# editar WIFI_SSID e WIFI_PASSWORD

# 2. Build e upload
pio run -t upload

# 3. Acesso
# http://mosfet.local/  (mesma rede Wi-Fi)
```

**Dependências de hardware mínimas:** ESP32 DevKit, 2× MCP4725, ADS1115, LT1013 (×2), 2N3904, shunt 100 Ω e 1 Ω.

Se algum periférico I²C estiver ausente, o firmware detecta na inicialização e exibe um modal descritivo antes de permitir o início da medição.

---

## Estrutura do Repositório

```
├── src/
│   ├── main.cpp                # Inicialização, servidor HTTP, handlers REST
│   ├── mosfet_controller.cpp   # Máquina de estados, sweep, calibração
│   ├── hardware_hal.cpp        # HAL: DAC/ADC interno e externo
│   └── ...
├── include/
│   ├── hardware_hal.h          # Constantes de hardware, inline shuntAmplifiedAdcToVoltage()
│   ├── mosfet_controller.h     # Configuração de sweep, erros globais, limites
│   └── version.h               # SOFTWARE_VERSION
├── scripts/
│   └── embed_web.py            # Pre-build: HTML/CSS/JS → PROGMEM arrays
├── web/                        # Fonte da UI (collection.js, visualization.js, ...)
├── documentation_master.md     # Documentação técnica completa (hardware, firmware, algoritmos)
└── platformio.ini
```

---

## Dispositivos Testados

CD4007 (CMOS gate), 2N7000 (switching), BS170 (AF/RF). Os três operam na faixa de 0–5 V da plataforma e têm perfis elétricos distintos (Vth, SS, Gm), servindo como conjunto de validação cruzada.

---

## Documentação Técnica

O arquivo [`documentation_master.md`](./documentation_master.md) contém a especificação completa: decisões de hardware com justificativas quantitativas, fluxogramas de firmware, algoritmos de cálculo (SS, Vth, Gm), formato do CSV e análise comparativa com o SMU Keysight B2902C.

---

**Projeto PIBIC — EESC-USP · Lucas Sales Duarte · Orientadora: Profa. Vanessa Cristina Pereira da Silva Venuto**
