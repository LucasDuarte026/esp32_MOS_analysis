---
trigger: always_on
---

# DOSSIÊ DE DIAGNÓSTICO ANALÓGICO E CALIBRAÇÃO

> **Problema Central:** Instabilidade e acoplamento cruzado (crosstalk) na leitura do Shunt amplificado ($A3$) quando o terminal de Dreno ($A1$) é conectado fisicamente ao ADC ADS1115.

---

## 1. Fenomenologia da Anomalia (O Problema)

Durante ensaios físicos, observou-se que a conexão do dreno do MOSFET ao pino $A1$ do ADS1115 causava uma elevação artificial na corrente calculada no Shunt (pino $A3$, amplificado $\approx 31.3\times$). Mesmo com o circuito em repouso ($V_{gs} = 0\text{ V}$), o canal de medição de corrente indicava vazamento inexistente. Ao desconectar a linha de dreno de $A1$, a leitura de $A3$ normalizava para $\approx 0\text{ V}$ (offset natural).

### Medições Diagnósticas de Bancada:
1. **Pino A1 Solto (Flutuante)**: Apresenta tensão inexplicável de $4.43\text{ V}$ medida via multímetro de alta impedância.
2. **Buffer Ativo Oscilando**: A tentativa de isolamento com um buffer unitário (LM358) entre o dreno e $A1$ causou oscilações devido à capacitância parasita do cabo de conexão, retificando ruído AC em uma componente DC indesejada de $600\text{ mV}$ na saída do buffer.

---

## 2. Hipóteses Diagnósticas Validadas

```text
               ADS1115 Switched-Capacitor Input
  A1 (VD) ───►  o─────────────┐
                              │    Sampling Capacitor (C_sh)
  A3 (IS) ───►  o───[MUX]─────┴───────[||]────────► ADC Core
                                       │
                                      GND
```

### 2.1 Charge Injection (Injeção de Carga no MUX Interno)
O ADS1115 possui arquitetura baseada em capacitores chaveados (*switched-capacitor*). Durante a amostragem rápida, o capacitor de amostragem interno ($C_{sh}$) carrega-se com o nível de tensão do canal previamente lido (ex: canal $A1$ a $4\text{ V}$). Ao comutar para o canal seguinte ($A3$, que lê pequenos sinais de shunt na escala de milivolts), a carga residual de $C_{sh}$ é "injetada" de volta na linha do shunt, gerando uma leitura de offset artificial (crosstalk severo).

### 2.2 Vazamento de Diodo ESD / Clone Falsificado
A presença de uma tensão de fuga estável de $4.43\text{ V}$ em pinos analógicos flutuantes sugere vazamento excessivo de corrente através dos diodos de proteção contra descargas eletrostáticas (ESD) conectados aos trilhos de alimentação. Isso é característico de:
* Danos térmicos/tensão sofridos pelo chip ADS1115.
* CIs clones ou falsificados que possuem isolamento e impedâncias internas de entrada muito inferiores às especificações do datasheet da Texas Instruments.

---

## 3. Soluções e Diretrizes de Mitigação

Para anular o crosstalk e garantir medições de nanoampères livres de ruído de injeção de carga, a IA deve orientar a aplicação das seguintes correções:

### 3.1 Filtros de Desacoplamento RC Locais (Hardware)
Adicionar pequenos capacitores cerâmicos de desacoplamento de **$10\text{ nF}$** conectados entre as entradas analógicas do ADS1115 e o GND analógico. 
* *Efeito*: O capacitor de $10\text{ nF}$ funciona como um reservatório de carga estável, absorvendo os picos de carga injetados pelo capacitor de amostragem $C_{sh}$ sem deslocar o nível de tensão DC sob medição.

### 3.2 Amortecimento de Cabos e Blindagem
* Substituir os jumpers longos analógicos por cabos curtos trançados ou cabos coaxiais blindados para reduzir acoplamentos capacitivos cruzados e captação de interferência eletromagnética (EMI).
* Inserir resistores em série de baixo valor ($100\ \Omega$) próximos às entradas do ADC para atenuar reflexões de alta frequência e oscilações induzidas por cabos.

### 3.3 Compensação por Software (Auto-Zero e Reta de Calibração)
* **Auto-Zero (Tara)**: O firmware deve sempre computar a média de 64 amostras do offset de $A3$ em standby (sem polarização) para subtrair esse erro dinâmico de todos os pontos futuros da curva de sublimiar.
* **Calibração de Reta de VD**: Compensar a queda e a fuga do resistor de $4.7\ \text{k}\Omega$ de proteção de $A1$ aplicando a equação de correção calibrada: $V_{d\_real} = (V_{d\_raw} \times 1.0015) + 0.0005\text{ V}$.
* **Substituição de Componente**: Caso a tensão de fuga flutuante no pino persistir acima de $1\text{ V}$ sob isolamento, o CI ADS1115 deve ser substituído por um circuito integrado genuíno certificado.
