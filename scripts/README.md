# Módulo: Scripts Utilitários (scripts/)

> **Documentação de Referência - Spec-Driven Development (SDD)**  
> **Hierarquia:** Nível 5 (Automação de Build, Integração e Validação de Dados)  
> **Status:** Ativo  

---

## 1. Objetivo do Módulo
Fornecer ferramentas auxiliares em Python para automatizar o ciclo de compilação (minificação e embarque de interface web), incrementação de versão semântica e análise estatística comparativa de dados de medição contra instrumentos de bancada de referência metrológica.

---

## 2. Responsabilidade Principal
Garantir a integridade física dos arquivos estáticos injetados na memória flash do microcontrolador (evitando estouro de espaço do firmware), manter a rastreabilidade do ciclo de versionamento automático de build e validar a acurácia dos dados obtidos comparando as curvas geradas com as medições de uma SMU Keysight B2902C comercial.

---

## 3. Funcionalidades Existentes
* **Minificação e Injeção Web (`embed_web.py`)**: Script executado automaticamente no pre-build hook do PlatformIO. Remove espaços em branco, minifica arquivos estáticos CSS, HTML e JavaScript da pasta `src/web/`, converte os binários em arrays de bytes hexadecimais C/C++ e cria o arquivo de cabeçalho `src/generated/web_dashboard.h`.
* **Incrementação de Versão Semântica (`increment_version.py`)**: Analisa o cabeçalho `include/version.h` e adiciona incrementalmente valores ao indicador de compilação (SNAPSHOT) a cada ciclo de gravação efetuado com sucesso via IDE/Terminal.
* **Validação Metrológica (`compare_iv.py`)**: Importa os arquivos CSV das varreduras geradas pelo ESP32 e plota gráficos comparativos contra os arquivos de dados extraídos da SMU Keysight B2902C, calculando métricas de desvio absoluto e erro percentual sistemático de $V_{th}$, $G_m$ e $SS$.

---

## 4. Dependências Internas e Externas
* **Dependências Internas**:
  * [src/web/](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web/) (insumo de arquivos para o minificador).
  * [include/version.h](/home/luska/Documents/projects/esp32_mosfet_analysis/include/version.h) (alvo de alteração de versão).
* **Dependências Externas (Python Libraries)**:
  * **Pandas**: Manipulação de dados CSV para análises.
  * **Matplotlib / Seaborn**: Geração gráfica dos relatórios comparativos.
  * **Scipy**: Lógica de regressão e diferenciação para validação das equações físicas.

---

## 5. Módulos Relacionados
* [src/](/home/luska/Documents/projects/esp32_mosfet_analysis/src/): Consome o arquivo gerado `web_dashboard.h` para servir as páginas do dashboard web.
* [include/](/home/luska/Documents/projects/esp32_mosfet_analysis/include/): Hospeda o versionamento semântico atualizado.

---

## 6. Arquivos Críticos e Detalhamento Técnico

1. **[embed_web.py](/home/luska/Documents/projects/esp32_mosfet_analysis/scripts/embed_web.py)**: Responsável por encapsular o frontend em flash (`PROGMEM`), gerando a constante `web_index_html_len` e correlatas.
2. **[compare_iv.py](/home/luska/Documents/projects/esp32_mosfet_analysis/scripts/compare_iv.py)**: Script central de validação metrológica do artigo científico. Calcula as divergências de regressão entre a plataforma ESP32 e o Keysight B2902C.

---

## 7. Fluxos Importantes

### 7.1 Compilação do Frontend no Ciclo PlatformIO
O script `embed_web.py` está configurado no arquivo `platformio.ini` como um script de pre-build:
```
[platformio.ini] -> extra_scripts = pre:scripts/embed_web.py
```
Isso garante que qualquer alteração nos arquivos JS, CSS ou HTML seja embarcada automaticamente a cada upload efetuado, prevenindo obsolescência do código embarcado.

---

## 8. Observações Técnicas e Débitos Identificados

* **⚠️ Hipótese - Ausência de `local_server.py`**: O arquivo `local_server.py` (servidor mock de testes local) é citado na documentação principal do projeto e em outros READMEs, porém não está presente fisicamente nesta pasta.  
  * *Implicação*: Os desenvolvedores não conseguem executar testes puros de JavaScript sem realizar o build completo do ESP32 no momento. Recomenda-se criar um servidor Python mock simples com endpoints emulados de `/api/status`, `/api/progress` e retorno de CSVs fictícios.
* **⚠️ Débito Técnico - Tratamento de Outliers em compare_iv.py**: O script de comparação pressupõe formatação perfeita nos arquivos de entrada. Caso ocorra aborto por sobrecorrente durante o ensaio da SMU ou do ESP32, as linhas incompletas quebram o alinhamento de índices do script de análise Python, exigindo limpeza manual prévia.
