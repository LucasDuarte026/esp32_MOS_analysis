# Caracterizador de MOSFETs ESP32 — Guia Mestre de Documentação (SDD)

> **Plataforma Metrológica de Caracterização Elétrica de Baixo Custo**  
> **Firmware:** v10.0.0 · **Target:** ESP32 Wroom 32D · **Licença:** Apache 2.0  
>
> Este repositório é governado pelas diretrizes do **Spec-Driven Development (SDD)**. Toda a documentação e códigos estão estruturados de forma modular e hierárquica (Bottom-Up), onde as restrições físicas analógicas da base ditam o comportamento das camadas superiores do sistema.

---

## 🌎 Índice de Documentação Spec-Driven (Bottom-Up)

Abaixo está o mapeamento completo da documentação do sistema, organizada de forma hierárquica. Para entender o sistema ou modificar um requisito, leia a documentação **de baixo para cima (das mãos à cabeça)**:

```
                  [6. Interface Web / Apresentação]  (Cabeça)
                                ▲
                                │
                   [5. API REST e Endpoints Web]
                                ▲
                                │
              [4. Orquestração e SO (FreeRTOS/FFat)]
                                ▲
                                │
             [3. Lógica de Controle / Matemática (PID)]
                                ▲
                                │
            [2. Abstração de Hardware / Drivers (HAL)]
                                ▲
                                │
               [1. Circuito Físico e Componentes]   (Mãos)
```

| Nível | Camada do Sistema | Documento de Especificação (Link Clicável) | Escopo Principal |
| :---: | :--- | :--- | :--- |
| **Geral** | **Objetivo do Sistema** | 📄 **[docs/objetivo_sistema.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/docs/objetivo_sistema.md)** | Propósito, dores resolvidas, atores e fluxos de negócio. |
| **Geral** | **Arquitetura Geral** | 📄 **[docs/arquitetura_sistema.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/docs/arquitetura_sistema.md)** | Visão arquitetural, fluxos de alteração e separação de camadas. |
| **6** | **Apresentação (WebUI)** | 🌐 **[src/web/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/web/README.md)** | JavaScript modular, CSS, HTML5, Plotly.js e Toasts. |
| **5** | **Scripts & Automação** | 🐍 **[scripts/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/scripts/README.md)** | Minificador `embed_web.py`, análise `compare_iv.py` e versionamento. |
| **4** | **Firmware (Sources)** | 🧠 **[src/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/README.md)** | Lógica C++, execução de sweeps, gravação em FAT e logs. |
| **3** | **Validação e Testes** | 🛠️ **[tests/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/tests/README.md)** | Testes de loop de controle, linearidade e calibração de DACs. |
| **2** | **Firmware (Headers)** | ⚙️ **[include/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/README.md)** | Cabeçalhos de hardware, definições físicas, structs e macros. |
| **1** | **Legado / Histórico** | 📁 **[docs/legacy/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/docs/legacy/)** | Arquivos de referência originais do PIBIC EESC-USP. |

---

## ⚙️ Regras do Ciclo de Mudança de Especificações

> [!WARNING]
> **Fluxo Estrito de Propagação (Bottom-Up):**  
> Alterações em especificações nunca devem ser feitas de cima para baixo. Se um comportamento do sistema for alterado (por exemplo, a faixa máxima de corrente suportada ou o resistor de shunt em uso):
> 
> 1. **Modifique a Camada 1 (Hardware)**: Registre o comportamento analógico e de pinagem em [include/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/README.md).
> 2. **Atualize a HAL (Camada 2)**: Altere os ganhos de leitura e lógicas de conversão em [src/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/README.md).
> 3. **Ajuste o Controlador (Camada 3)**: Altere as tolerâncias de loop e constantes em [src/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/README.md) e realize testes em [tests/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/tests/README.md).
> 4. **Ajuste a API e Interface (Camada 5 e 6)**: Valide a integridade dos payloads JSON no frontend em [src/web/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/web/README.md).
> 5. **Atualize o Versionamento**: Incremente a versão do software e compile usando as ferramentas listadas em [scripts/README.md](file:///home/luska/Documents/projects/esp32_mosfet_analysis/scripts/README.md).

---

## 🚀 Como Iniciar

1. **Configurar Credenciais**:
   Copie o arquivo de secrets de exemplo na pasta de headers e configure suas credenciais de Wi-Fi e SMTP de e-mail:
   ```bash
   cp include/secrets.h.example include/secrets.h
   ```
2. **Compilação e Upload**:
   Com o PlatformIO instalado, conecte o ESP32 via USB e execute:
   ```bash
   pio run -t upload
   ```
3. **Acesso**:
   Acesse a interface no navegador do computador conectado na mesma rede através do endereço mDNS:
   ```
   http://mosfet.local/
   ```

*Para maiores detalhes de funcionamento de hardware e calibração, acesse o guia de [Objetivo do Sistema](file:///home/luska/Documents/projects/esp32_mosfet_analysis/docs/objetivo_sistema.md) e [Arquitetura do Sistema](file:///home/luska/Documents/projects/esp32_mosfet_analysis/docs/arquitetura_sistema.md).*
