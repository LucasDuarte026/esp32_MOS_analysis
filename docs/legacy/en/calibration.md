# Calibration, Filtering & Diagnostics

To extract metrology-grade electrical parameters using low-cost hardware components, the firmware compensates for physical limitations (such as voltage drops, offsets, and high-frequency noise) through software correction algorithms.

---

## 1. Hardware Calibration

### 1.1 Drain Voltage ($V_d$) Calibration (Resistor Compensation)
To protect the ADS1115 ADC from transient voltage spikes (up to $12\text{ V}$), a **$4.7\ \text{k}\Omega$ protection resistor** is placed in series with the $V_d$ measurement line. However, this resistor introduces two error sources at the high-impedance ADC input:
1. **Gain Error**: A voltage divider is formed between the $4.7\ \text{k}\Omega$ protection resistor and the internal input impedance of the ADS1115 ($\approx 3\ \text{M}\Omega$).
2. **Offset Error**: A voltage drop is generated across the protection resistor by the ADC's internal leakage current ($\approx 100\text{ nA}$).

These errors are corrected in software using a linear calibration model:

$$V_{d\_real} = (V_{d\_raw} \times F_{scale}) + V_{offset}$$

#### 2-Point Calibration Protocol
To calculate the correction parameters:
1. **Low Calibration Point**: Bias the Drain to $\approx 0.1\text{ V}$. Record the real voltage with a benchtop multimeter ($V_{multimeter}$) and the raw voltage reported by the ADC ($V_{adc}$).
2. **High Calibration Point**: Bias the Drain to $\approx 4.5\text{ V}$. Record $V_{multimeter}$ and $V_{adc}$.
3. Solve the linear equation $y = mx + b$ (where $y = V_{multimeter}$ and $x = V_{adc}$):
   * $F_{scale} = m \approx 1.0015$ (gain factor)
   * $V_{offset} = b \approx 0.0005\text{ V}$ (offset correction)

The calculated $V_{d\_real}$ is clamped at $0.0\text{ V}$ to prevent negative voltage readings caused by noise:

```cpp
float vd_real_volts = (vd_raw_volts * fator_escala_vd) + offset_vd_volts;
if (vd_real_volts < 0.0f) {
    vd_real_volts = 0.0f;
}
```

---

## 2. Software Auto-Zero (Tare Engine)

To compensate for the input offset voltage of the LT1013/LM358 operational amplifiers and the offset error of the ADS1115, an **Auto-Zero** routine is executed before starting any sweep.

With the DUT in standby (Gate Voltage $V_{GS} = 0\text{ V}$ and Drain Voltage $V_{DS} = 0\text{ V}$), the system:
1. Takes **64 readings** of the high-precision shunt channel ($A3$).
2. Computes the average value.
3. Saves this average as the **System Offset** (`A3_DC_offset`).
4. Subtracts this offset from all subsequent readings on that channel.

This step reduces measurement errors at the bottom of the subthreshold region (currents below $100\text{ nA}$).

---

## 3. Operational Range & Saturaion Safeguards

The LM358/LT1013 operational amplifiers used in the shunt amplification stage are powered by a simple single supply rail ($5\text{ V}$). This limits their linear output swing to $\approx 3.4\text{ V}$ before entering saturation.

To prevent clipping, the firmware implements an **Auto-Range** switch:
* As long as the voltage on $A3$ is **$< 3.3\text{ V}$**, the current calculation uses the amplified $A3$ reading ($100\ \Omega$ shunt amplified $31.3039\times$).
* Once $A3$ rises **$\ge 3.3\text{ V}$**, the firmware switches to the raw $A0$ channel ($1\ \Omega$ direct shunt).

---

## 4. Hardware Diagnostics: Crosstalk & Sample & Hold Leakage

During the prototyping phase, a severe crosstalk anomaly was identified: when the $V_d$ line was connected to pin $A1$ of the ADS1115, the voltage read on the amplified shunt channel ($A3$) drifted higher, indicating a false leakage current even when the MOSFET was completely off ($V_{GS} = 0\text{ V}$).

### 4.1 Diagnostic Observations
* **Floating Voltage**: When disconnected from the circuit, pin $A1$ of the ADS1115 floats at a voltage of **$4.43\text{ V}$**.
* **Active Buffer Oscillation**: Placing a unity-gain op-amp buffer between the Drain and $A1$ resulted in high-frequency oscillations due to the parasitic capacitance of the connection cable, causing AC noise rectification ($600\text{ mV}$ DC offset).

### 4.2 Root Cause Analysis

```
       ADS1115 Multiplexer
  A1 ───►  o─────────────┐
                         │   Sample & Hold Capacitor (C_sh)
  A3 ───►  o───[MUX]────┴───────[||]────────► ADC Core
                                 │
                                GND
```

1. **Charge Injection (Sample & Hold Crosstalk)**: The ADS1115 uses a switched-capacitor Sample & Hold architecture. During channel switching, the internal sampling capacitor ($C_{sh}$) charges to the voltage level of the previous channel. If a channel has high impedance (like $A1$ with its $4.7\ \text{k}\Omega$ protection resistor or a floating line), the charge on $C_{sh}$ is injected back into the analog measurement line during comutation, corrupting the subsequent reading.
2. **Component Degradation / Counterfeit ICs**: A floating voltage of $4.43\text{ V}$ on $A1$ indicates leakage through the internal ESD protection diodes. This is a common issue with counterfeit ADS1115 modules or ICs that have been damaged by overvoltage transients.

### 4.3 Mitigation Strategies
To resolve these issues, the following hardware adjustments are recommended:
* **RC Low-Pass Filtering**: Add a small decoupling capacitor ($10\text{ nF}$) between the ADC input pins and GND, forming an RC low-pass filter with the series protection resistors. This capacitor acts as a charge reservoir, absorbing the charge injection from $C_{sh}$ without shifting the DC voltage level.
* **Shielded Cabling**: Minimize the length of the jumpers connecting the ADS1115 module to the main PCB, and use twisted pairs or shielded lines for the analog signals to reduce capacitive coupling and electromagnetic interference.
* **Component Validation**: Verify the input leakage currents of the ADS1115 module under load. If leakage exceeds datasheet specifications, replace the module with a genuine unit from a certified distributor.
