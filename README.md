# ESP32 MOSFET Analysis Dashboard

## 🎯 Propósito

**Projeto principal** de análise MOSFET com dashboard web completo e interface profissional.

## 📋 O que faz

- **Controle de varredura Vgs** (tensão gate-source)
- **Aquisição de dados Ids** (corrente drain-source)
- **Dashboard web interativo** com 3 abas:
  - **Coleta de Dados:** Configuração e início de medições
  - **Visualização:** Gráficos interativos (Ids, Gm, SS, Vt)
  - **Email:** Envio de relatórios
- **API REST** para comunicação com scripts Python
- **Armazenamento de medições** no LittleFS

## 🔧 Hardware

### Pinos ESP32:

- **DAC (GPIO 25):** Controle de tensão Vgs
- **ADC (GPIO 34):** Leitura de corrente Ids
- **LED (GPIO 2):** Indicador de status

### Circuito Externo:

- Fonte de alimentação controlada
- Circuito de medição MOSFET
- Conversão I-V para leitura ADC

## 🚀 Como Usar

### 1. Configurar WiFi

1. Copie o arquivo de exemplo:
   ```bash
   cp include/secrets.h.example include/secrets.h
   ```

2. Edite `include/secrets.h` com suas credenciais:
   ```cpp
   #define WIFI_SSID "SUA_REDE"
   #define WIFI_PASSWORD "SUA_SENHA"
   ```

> ⚠️ **Privacidade:** O arquivo `secrets.h` já está no `.gitignore` e não será enviado para o repositório. Use-o para suas senhas locais.

### 2. Compilar e Fazer Upload

```bash
cd esp32_mosfet_analysis
pio run -t upload
pio device monitor
```

### 3. Acessar Dashboard

Após conectar ao WiFi, acesse: `http://IP_DO_ESP32/` ou `http://esp32-mosfet.local/`

## 📊 Dashboard Interface

### Aba 1: Coleta de Dados

- Configurar parâmetros de medição:
  - Vgs inicial/final
  - Passo de tensão
  - Tempo de estabilização
  - Resistor shunt
- Iniciar/parar coleta
- Monitor de progresso em tempo real

### Aba 2: Visualização de Dados

- Gráficos interativos (Plotly.js)
- Toggles para curvas:
  - Ids (corrente)
  - Gm (transcondutância)
  - SS (subthreshold swing)
  - Segunda derivada
  - Vt (tensão de limiar)
- Métricas calculadas

### Aba 3: Envio de Dados

- Compor e enviar relatórios por email
- Anexar dados (CSV, JSON, PDF)
- Incluir gráficos

## 🔍 API REST

### GET `/api/status`

Status do sistema:

```json
{
  "status": "ready",
  "device": "ESP32-MOSFET"
}
```

### POST `/api/start`

Iniciar medição:

```json
{
  "vgs_start": 0.0,
  "vgs_end": 3.5,
  "vgs_step": 0.05,
  "rshunt": 100,
  "settling_ms": 5,
  "filename": "mosfet_data"
}
```

### GET `/api/data`

Obter dados de medição:

```json
{
  "data": [
    {"timestamp": 1234, "vgs": 0.0, "vsh": 0.001},
    {"timestamp": 1239, "vgs": 0.05, "vsh": 0.002}
  ],
  "count": 71
}
```

### GET `/api/files`

Listar medições salvas no ESP32.

### GET `/api/files/download?file=nome.csv`

Baixar arquivo de medição específico.

## 📁 Estrutura

```
esp32_mosfet_analysis/
├── src/
│   ├── main.cpp               # Código principal + handlers HTTP
│   ├── mosfet_controller.cpp  # Controlador MOSFET
│   ├── file_manager.cpp       # Gerenciador de arquivos
│   ├── monitoring_task.cpp    # Monitoramento do sistema
│   ├── log_buffer.cpp         # Buffer de logs
│   ├── web_ui.cpp             # Interface web
│   └── web/
│       ├── dashboard.html     # Dashboard HTML
│       ├── dashboard.css      # Estilos
│       └── dashboard.js       # JavaScript
├── include/
│   ├── mosfet_controller.h
│   ├── file_manager.h
│   ├── monitoring_task.h
│   ├── log_buffer.h
│   ├── web_ui.h
│   └── wifi_credentials.h     # Configuração WiFi (via build flags)
├── scripts/
│   └── embed_web.py           # Script para embeber HTML
└── platformio.ini             # Configuração PlatformIO
```

## 🔗 Integração com Python

Este projeto trabalha em conjunto com scripts Python em `../analysis/`:

- **Comunicação serial** para controle direto
- **API HTTP** para acesso remoto
- **Processamento offline** de dados coletados

## 🚧 Status de Desenvolvimento

**⚠️ Em Desenvolvimento**

Funcionalidades implementadas:
- ✅ Servidor web com dashboard interativo
- ✅ API REST completa
- ✅ Armazenamento de medições (LittleFS)
- ✅ Sistema de logs em tempo real
- ✅ Monitoramento de temperatura e memória
- ✅ Validação de segurança (path traversal, XSS, CORS)
- ✅ Classe MOSFETController com varredura VGS

Próximos passos:
- [ ] Implementar controle DAC para Vgs
- [ ] Implementar leitura ADC calibrada para Ids
- [ ] Calcular Gm, Vt, SS automaticamente
- [ ] Integrar gráficos Plotly no dashboard

---

**Status:** 🚧 Projeto Principal (Em Desenvolvimento)
