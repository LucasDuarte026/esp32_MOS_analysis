# Módulo: Web Interface & Assets (src/web/)

> **Documentação de Referência - Spec-Driven Development (SDD)**  
> **Hierarquia:** Nível 6 (Apresentação, Dashboard Web e UX - Cabeça)  
> **Status:** Ativo  

---

## 1. Objetivo do Módulo
Implementar a interface gráfica responsiva e interativa que permite ao usuário parametrizar ensaios, acompanhar varreduras em tempo real, analisar as curvas de MOSFETs graficamente e exportar dados via download ou e-mail.

---

## 2. Responsabilidade Principal
Prover uma experiência de uso de alta qualidade no navegador (Zero-Instalação) que esconda a complexidade de hardware e firmware do microcontrolador, fornecendo gráficos dinâmicos de alta performance e validações rigorosas que previnam requisições inválidas ao ESP32.

---

## 3. Funcionalidades Existentes
* **Configuração de Sweeps**: Formulário dinâmico com limites e valores padrão pré-configurados para evitar erros operacionais.
* **Hardware Check Visual**: Painel dinâmico que exibe a resposta I2C dos DACs e ADC na inicialização da página.
* **Gráficos Multi-Eixo**: Renderização de curvas de corrente linear ($I_{ids} \times V_{gs}$), escala de logaritmo ($\log_{10}(I_{ids}) \times V_{gs}$), transcondutância ($G_m$) e curvas de dreno ($I_{ids} \times V_{ds}$) usando Plotly.js.
* **Projeção de Tangente e Marcador**: Overlay visual da reta tangente do Subthreshold Swing ($SS$) e marcador do ponto exato de Tensão de Limiar ($V_{th}$).
* **Exportação SMTP**: Painel com formulário para envio seguro das varreduras em formato CSV diretamente para o e-mail do usuário.

---

## 4. Dependências Internas e Externas
* **Dependências Internas**:
  * [src/web_ui.cpp](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web_ui.cpp) (endpoint C++ que serve este frontend compactado em PROGMEM).
  * [scripts/embed_web.py](/home/luska/Documents/projects/esp32_mosfet_analysis/scripts/embed_web.py) (script que minifica os arquivos desta pasta para embarque).
* **Dependências Externas (CDNs)**:
  * **Plotly.js (v2.24.1)**: Biblioteca de plotagem científica interativa (carregada via CDN no browser).

---

## 5. Módulos Relacionados
* [src/](/home/luska/Documents/projects/esp32_mosfet_analysis/src/): Módulo backend que expõe os endpoints REST HTTP consumidos pelas rotinas JavaScript deste módulo.
* [scripts/](/home/luska/Documents/projects/esp32_mosfet_analysis/scripts/): Contém ferramentas de compilação dos recursos e o servidor local de testes Python.

---

## 6. Arquivos Críticos e Organização de Código

1. **[index.html](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web/index.html)**: Ponto de entrada HTML. Contém os containers do dashboard e o formulário de parâmetros.
2. **[core.js](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web/core.js)**: Utilitários globais. Implementa Toasts (`showToast()`), logs categorizados (`dbg()`) e constantes de limite.
3. **[collection.js](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web/collection.js)**: Lógica de controle e polling. Gerencia o início/cancelamento do sweep e o pooling contínuo em `/api/progress`.
4. **[visualization.js](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web/visualization.js)**: Core matemático do frontend. Faz o parse do CSV de FFat, plota os gráficos do Plotly e renderiza o cálculo de $V_{th}$, $G_m$ e $SS$.
5. **[dashboard.css](/home/luska/Documents/projects/esp32_mosfet_analysis/src/web/dashboard.css)**: Estilos da interface, incluindo responsividade para telas de dispositivos móveis.

---

## 7. Fluxos Importantes

### 7.1 Lógica de Logging e Debg
Para garantir desempenho no browser do cliente e evitar poluição do console, é proibido o uso de `console.log`. A rotina de depuração deve usar o wrapper `dbg()` de `core.js`:

```javascript
// Exemplo de chamada correta no frontend:
dbg('API', 'Enviando payload de varredura:', payload);
dbg('MATH', 'Média de oversampling calculada:', valor);
```

### 7.2 Fluxo de Teste Standalone (Sem Hardware)
Para testar alterações de layout e lógica do JavaScript sem precisar compilar e gravar a flash do ESP32 constantemente:
1. Navegue até a pasta `scripts/` e execute o comando `python local_server.py`.
2. Acesse `http://localhost:8000/` no seu navegador.
3. O script carrega um servidor Python mock que responde com dados estáticos de teste simulando a API REST do ESP32.

---

## 8. Observações Técnicas e Débitos Identificados

* **⚠️ Débito Técnico - Plotly CDN Dependency**: O frontend depende da CDN externa para carregar o arquivo `plotly-latest.min.js`. Caso a bancada do laboratório não tenha acesso à internet (intranet pura com o ESP32 operando como Access Point), os gráficos não serão renderizados.
  * *Solução recomendada*: Armazenar uma versão enxuta ou simplificada do Plotly diretamente na partição de flash do ESP32, servindo localmente.
* **⚠️ Débito Técnico - Renderização Única de Curvas**: O frontend renderiza apenas uma curva CSV por vez. Desejos futuros incluem a implementação de um toggle para visualização múltipla (família de curvas na mesma varredura $I_{ids} \times V_{ds}$ no Plotly, coloridas individualmente com legendas indicando a tensão de gate aplicada).
