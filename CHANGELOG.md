# Changelog

Todas as alterações notáveis neste projeto serão documentadas neste arquivo.

O formato é baseado em [Keep a Changelog](https://keepachangelog.com/pt-BR/1.0.0/)
e este projeto adere ao [Semantic Versioning](https://semver.org/lang/pt-BR/).

---

## [12.0.16] - 2026-08-11

### Adicionado
- **Documentação Modular SDD (Spec-Driven Development):** Criação de manuais técnicos de referência e responsabilidades por camada em `include/README.md`, `scripts/README.md`, `src/README.md`, `src/web/README.md` e `tests/README.md`.
- **Dossiê de Diagnóstico Analógico:** Registro detalhado sobre fenômenos de injeção de carga (*charge injection*), crosstalk no MUX do ADS1115 e diretrizes de mitigação em hardware e software em `.agents/rules/analog-diagnostics.md`.
- **Diretrizes e Governança de Agentes:** Formalização das regras de desenvolvimento hierárquico em `.agents/AGENTS.md` e `.agents/rules/development-guidelines.md`.
- **Skill de Versionamento (`version_control`):** Assistente de terminal para automação de commits semânticos, análise de SemVer e gestão de branches.
- **Assets e Diagramas Atualizados:** Inclusão de diagramas de conexões e esquemáticos atualizados em `images/`.

---

## [12.0.0] - 2026-06-18

### Modificado
- **Calibração e Precisão Metrológica:**
  - Implementação de ganhos e offsets reais medidos em bancada de hardware (Ganho: `31.43`, Offset: `-9.88 mV`).
  - Refatoração total do script de calibração automática para maior robustez e repetibilidade.
  - Remoção de filtros de software que mascaravam o ruído em correntes ultra baixas, permitindo análise fidedigna da física na região de sublimiar (*subthreshold*).
- **Interface Web e Visualização:**
  - Otimização do pipeline de renderização dos gráficos com `Plotly.react` para suportar grandes volumes de dados sem engasgos na UI.
  - Correção e estabilização de rotinas de download de CSV via streaming assíncrono.
  - Suporte a telas High-DPI nas ferramentas de comparação offline.
- **Manutenção e Limpeza:**
  - Limpeza e remoção de lógicas obsoletas de fallback no HAL.
  - Documentação de decisões de arquitetura e constantes diretamente no código-fonte.

---

## [11.0.0] - 2026-06-10

### Adicionado
- **Segurança de Hardware (Hardware Safety):** Monitoramento de potência em tempo real no shunt ($1.25\text{ W}$) nos loops iterativos de calibração de DAC para prevenir sobrecarga e queima de componentes.
- **Modo de Precisão em Tempo de Execução:** Alternância dinâmica de leitura amplificada (A3) via interface web.
- **Fuso Horário:** Suporte nativo a sincronização NTP e fuso horário de São Paulo (UTC-3).
- **Auto-Swap de Limites:** Correção automática de faixas de sweep caso os limites inicial e final sejam invertidos na UI.

### Modificado
- **Motor Matemático (Math Engine):**
  - Expansão do ajuste de sublimiar para baixas correntes até $10\text{ nA}$ (otimizado para shunt de $1\text{ k}\Omega$).
  - Regressão de Subthreshold Slope ($SS$) refinada para priorizar a região de maior inclinação com $R^2 \ge 0.85$.
  - Depreciação do $V_{th\_SS}$ em favor da extrapolação por pico de transcondutância ($V_{th\_Gm}$).

---

## [10.2.0] - 2026-06-05

### Adicionado
- **Suporte a Shunt de $100\ \Omega$:** Capacidade de selecionar e calibrar shunt de $100\ \Omega$ para aumento de resolução em baixas correntes.
- **Multiplexação ADS1115 Completa:** Leitura dos 4 canais analógicos (A0, A1, A2, A3) para monitoramento simultâneo de corrente e tensões reais de Gate, Dreno e Shunt.

---

## [10.0.0] - 2026-05-28

### Adicionado
- **Documentação Mestre:** Centralização do manual de calibração, especificação do firmware e arquitetura analógica.
- **Sincronização de Precisão com LT1013:** Acoplamento de amplificador de instrumentação de baixo offset para amplificação do shunt com ganho $\approx 31.3\times$.

---

## [9.2.0] - 2026-05-15

### Adicionado
- **Auto-Range PGA:** Chaveamento dinâmico de leitura entre canal amplificado (A3) e canal direto (A0) de acordo com o nível de corrente.
- **Visualização de $V_{sh}$:** Plotagem dedicada da queda de tensão no resistor de shunt no dashboard.

---

## [7.1.0] - 2026-04-20

### Adicionado
- **Cálculo de Tensões Reais nos Terminais ($V_{ds\_true}$ e $V_{gs\_true}$):**
  - $V_{ds\_true} = V_{d\_read} - V_{sh}$
  - $V_{gs\_true} = V_{g\_read} - V_{sh}$
- **Estrutura de 9 Colunas no CSV:** `timestamp`, `vd`, `vg`, `vd_read`, `vg_read`, `vsh`, `vds_true`, `vgs_true`, `ids`.

### Corrigido
- **Panic no Core 1 (`LoadProhibited`):** Correção no `log_buffer.h` onde referências a strings temporárias geravam *dangling pointers*.

---

## [6.0.0] - 2026-03-30

### Adicionado
- **Seletor de Ganho do ADC:** Configuração de PGA dinâmico via Web UI.
- **Metadados de Ensaio:** Cabeçalho de metadados gravado no início do arquivo CSV gerado pelo ESP32.

---

## [5.0.0] - 2026-03-10

### Modificado
- **Refatoração Geral de Código:** Padronização de headers C++, modularização em namespaces (`hal::`, `mosfet::`, `webui::`, `math_engine::`) e remoção de boilerplate.

---

## [1.0.0] - 2026-01-15

### Adicionado
- **Lançamento Inicial:** Plataforma de caracterização de MOSFETs baseada em ESP32 com MCP4725 (DAC), ADS1115 (ADC) e Web UI assíncrona.
