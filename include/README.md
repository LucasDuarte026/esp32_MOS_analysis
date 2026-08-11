# Módulo: Firmware Headers (include/)

> **Documentação de Referência - Spec-Driven Development (SDD)**  
> **Hierarquia:** Nível 2 e Nível 3 (Definições Físicas, Configurações de Malha e Interfaces de Hardware)  
> **Status:** Ativo  

---

## 1. Objetivo do Módulo
Centralizar as definições de constantes físicas de hardware, limites de tolerância de malha analógica, estruturas de dados para configuração de sweeps, credenciais de segurança e as assinaturas de classe do firmware C++.

---

## 2. Responsabilidade Principal
Garantir que as especificações analógicas de baixo nível (valores de resistores, ganhos de amplificador, GPIOs) e as configurações lógicas de controle de alto nível estejam unificadas e acessíveis a todas as rotinas em C++ de forma consistente, operando como o dicionário de tipos e limites do sistema.

---

## 3. Funcionalidades Existentes
* **Mapeamento de Pinos (GPIO)**: Declarações estáticas de pinagem do ESP32 (SDA/SCL I2C, GPIO14 para active bleeder, GPIO12 para debug jumper, LED nativo).
* **Configuração de Calibração Analógica**: Armazena coeficientes estáticos e constantes calibradas de ganho do LT1013 (`SHUNT_AMP_GAIN` $\approx 31.3039$) e tara padrão.
* **Structs de Varredura**: Estruturas de dados `SweepConfig` para tráfego de parâmetros (modo de varredura, faixa de VGS/VDS, tempos de acomodação, taxas de amostragem).
* **Versionamento Lógico**: Define a tag de versão do firmware (`SOFTWARE_VERSION` em formato `MAJOR.MINOR.SNAPSHOT`).
* **Secrets & Credentials Template**: Arquivo de configuração de credenciais Wi-Fi e e-mail fora do controle de versão.

---

## 4. Dependências Internas e Externas
* **Dependências Internas**:
  * [src/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/) (módulo executor que inclui estes arquivos de cabeçalho).
* **Dependências Externas**:
  * **Arduino API e ESP32 Core**: Para definições de tipos primitivos e estruturas do microcontrolador (e.g., `GPIO`, `IPAddress`).
  * **Adafruit Drivers**: Estruturas de suporte de biblioteca do ADS1115 e MCP4725.

---

## 5. Módulos Relacionados
* [src/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/src/): Consome todas as definições contidas nesta pasta para instanciar objetos e aplicar lógicas de varredura.
* [scripts/](file:///home/luska/Documents/projects/esp32_mosfet_analysis/scripts/): O script `increment_version.py` lê e atualiza programaticamente o arquivo `version.h` a cada build de produção.

---

## 6. Arquivos Críticos e Definições de Baixo Nível

1. **[hardware_hal.h](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/hardware_hal.h)**: Contém o mapeamento de pinagem, limites analógicos de FSR do ADS1115, e a macro inline crítica de conversão de tensão do shunt amplificado:
   ```cpp
   inline float shuntAmplifiedAdcToVoltage(float adcVolts) {
       return (adcVolts / SHUNT_AMP_GAIN) - SHUNT_AMP_OFFSET;
   }
   ```
2. **[mosfet_controller.h](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/mosfet_controller.h)**: Define a struct `SweepConfig`, a máquina de estados do controlador (`MOSState`), limites operacionais de varredura (VGS de 0 a 5V) e parâmetros do PID simples (tolerância de erro de convergência de 2 mV).
3. **[version.h](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/version.h)**: String central que dita a versão do firmware em execução.
4. **[secrets.h.example](file:///home/luska/Documents/projects/esp32_mosfet_analysis/include/secrets.h.example)**: Molde para o arquivo `secrets.h` (excluído do repositório) que armazena o SSID e a senha do Wi-Fi de bancada, bem como as credenciais de SMTP para e-mail.

---

## 7. Fluxos Importantes

### 7.1 Hierarquia Bottom-Up de Ajuste Físico
Sempre que o hardware mudar, siga a ordem de alteração nos arquivos deste módulo:
```
[Mudança Física: Ex. Alteração do ganho do LT1013 para 45x]
  │
  ├──► 1. Altere SHUNT_AMP_GAIN no arquivo include/hardware_hal.h
  ├──► 2. Se necessário, recalibre SHUNT_AMP_OFFSET no mesmo arquivo
  ├──► 3. Verifique se os limites de Auto-Range em include/mosfet_controller.h ainda são válidos
  └──► 4. Recompile e atualize a versão de SNAPSHOT no include/version.h
```

---

## 8. Observações Técnicas e Débitos Identificados

* **⚠️ Débito Técnico - Calibração Hardcoded**: Os valores de `fator_escala_vd` e `offset_vd_volts` (para correção da queda do resistor de proteção de 4.7k) e do offset `SHUNT_AMP_OFFSET` estão definidos estaticamente no código. No modelo Spec-Driven ideal, esses dados deveriam ser carregáveis via EEPROM do ESP32 ou arquivo de configuração em FFat, permitindo a calibração de novas placas sem necessidade de compilação.
* **⚠️ Débito Técnico - Estrutura de Credenciais Duplicadas**: Existe um acúmulo de arquivos de credenciais (`secrets.h` e `wifi_credentials.h`). Recomenda-se unificar a gestão de credenciais Wi-Fi e SMTP em um único arquivo `secrets.h`.
