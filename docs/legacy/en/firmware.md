# Firmware Architecture & Algorithms

The firmware is written in C++14 using PlatformIO on top of the Arduino framework for ESP32 (which wraps the native ESP-IDF). It relies heavily on FreeRTOS features to maintain real-time measurement deadlines while serving web requests.

---

## 1. Multi-Core Architecture & Task Isolation

The ESP32 features a dual-core Xtensa LX6 CPU. The firmware partitions its execution to prevent resource conflicts and network jitter from corrupting measurements:

```
            ┌────────────────────────────────────────────────────────┐
            │                     ESP32 Processor                    │
            │                                                        │
            │     ┌────────────────────┐      ┌───────────────────┐  │
            │     │       CORE 0       │      │      CORE 1       │  │
            │     │  ────────────────  │      │  ───────────────  │  │
            │     │  ESPAsyncWebServer │      │  Measurement Task │  │
            │     │  REST Endpoints    │      │  Closed-Loop PID  │  │
            │     │  mDNS Service      │      │  Math Engine (SS) │  │
            │     │  Wi-Fi/TCP Stack   │      │  FFat CSV Write   │  │
            │     └─────────┬──────────┘      └─────────┬─────────┘  │
            └───────────────┼───────────────────────────┼────────────┘
                            │                           │
                            └─────► FreeRTOS Mutex ◄────┘
                                   [I2C Bus Lock]
```

* **Core 0 (System Task)**: Manages network communication, routes HTTP endpoints, handles REST API JSON parsing, and maintains the mDNS name resolution. Web server responses take `< 50 ms` even during active sweeps.
* **Core 1 (Measurement Task)**: Handles hardware control loops, reads sensors, applies calculations, and writes data to flash memory. Pinned using FreeRTOS `xTaskCreatePinnedToCore` with a dedicated $8\text{ KB}$ stack.

### 1.1 I2C Bus Mutex Locking
Because Core 0 might attempt to query device statuses or system stats while Core 1 is actively reading voltages from the ADC or writing to the DACs, a FreeRTOS `SemaphoreHandle_t` (Mutex) protects all I2C bus transactions. Every I2C transaction is wrapped inside a lock-and-release block to prevent data frame corruption.

---

## 2. Closed-Loop Control Loop (Calibration)

In saturation and triode regions, the current ($I_{ds}$) flowing through the source resistor ($R_{shunt}$) causes a voltage drop ($V_{sh}$). This drop degenerates the actual terminal voltages of the MOSFET:

$$V_{GS\_true} = V_{G\_applied} - V_{sh}$$

$$V_{DS\_true} = V_{D\_applied} - V_{sh}$$

To maintain exactly the target voltages specified by the sweep parameters, the firmware implements a software-based closed-loop calibration routine at each measurement step:

```
[Target VGS / VDS]
       │
       ▼
   Apply DAC (Probe = Target + Vsh_last)
       │
       ▼
   Wait (Settling Time)
       │
       ▼
   Read Real Voltages (A1, A2, Shunt)
       │
       ▼
   Calculate Error = Target - Real
       │
       ├─► If |Error| < 2 mV ──► CONVERGED (Proceed to read)
       │
       └─► If |Error| >= 2 mV ─► Probe = Probe + Error ──► Re-apply (Max 10 Iterations)
```

### 2.1 Double Cross-Validation
A change in Gate voltage alters the channel resistance, shifting $I_{ds}$ and thus $V_{sh}$. This shift perturbs $V_{DS\_true}$. The firmware prevents this cross-influence by performing double cross-validation: after converging $V_{GS}$, it verifies $V_{DS}$. If $V_{DS}$ deviates by more than $2\text{ mV}$, it recalibrates $V_{DS}$ and re-checks $V_{GS}$ in a nested feedback loop.

---

## 3. Oversampling and Outlier Rejection Math

To filter white noise (thermal noise, quantization noise) and eliminate spikes on the I2C bus or EMI from the ESP32 Wi-Fi antenna, the firmware uses an oversampling pipeline combined with a **Trimmed Mean** filter.

### 3.1 Noise Reduction Principle
By averaging $N$ independent measurements, the standard deviation of random white noise ($\sigma$) decreases by a factor of $\sqrt{N}$:

$$\sigma_N = \frac{\sigma_1}{\sqrt{N}}$$

This process increases the Effective Number of Bits (ENOB) of the measurement:

$$\Delta\text{ENOB} = \frac{\log_2(N)}{2}$$

For $N = 64$ samples, we achieve a theoretical gain of **$3\text{ bits}$** of resolution, pushing the effective resolution of the ADS1115 close to **$19\text{ bits}$**.

### 3.2 Trimmed Mean Filtering (10% Outlier Rejection)
For each final measurement point:
1. **Collect $N$ samples** ($N \in [16, 64]$ depending on settings).
2. **Sort the array** using *Insertion Sort*.
   > [!TIP]
   > Insertion Sort has a time complexity of $O(N^2)$, but it runs entirely on the stack without allocating heap memory (`malloc`), preventing heap fragmentation on the ESP32.
3. **Discard the top 10% and bottom 10%** of the sorted samples. This step eliminates transient spikes, high-frequency digital noise, and ADC read errors.
4. **Average the remaining 80%** of the samples.

---

## 4. Dual Shunt Auto-Range Logic

To achieve a wide dynamic range (measuring currents from nanoamperes to hundreds of milliamperes) without saturating the op-amps or under-resolving signals, the system dynamically switches channels:

* **Primary High-Sensitivity Channel (A3)**: Reads the shunt voltage amplified $31.3039\times$ by the LT1013.
* **Secondary Wide-Range Channel (A0)**: Reads the raw shunt voltage.

```
                  Measure raw voltage on A3 (Amplified Shunt)
                                     │
                 ┌───────────────────┴───────────────────┐
                 ▼                                       ▼
           Is A3 < 5.0 V?                          Is A3 >= 5.0 V?
           [Amplifier Active]                      [Amplifier Saturated]
                 │                                       │
                 ▼                                       ▼
   Use A3: Vsh = V_A3 / 31.3039              Use A0: Vsh = V_A0 (Direct)
```

The transition between these channels is computed on-the-fly and is completely seamless to the user.

---

## 5. Streaming Direct to Flash (FFat)

Since the ESP32 has limited RAM ($520\text{ KB}$ SRAM), storing a high-resolution sweep with 1,000 steps and multiple floating-point columns could consume upwards of $40\text{ KB}$ of heap, risking memory allocation failures.

To solve this, the firmware uses **Direct-to-Flash Streaming**:
1. An empty file is opened in the internal FAT partition (`FFat`).
2. The CSV header is immediately written.
3. At each step of the sweep, the measured values are printed directly to the file buffer.
4. The file buffer is flushed every 50 lines to prevent data loss.
5. RAM is only used to store the current curve's values to compute parameters ($V_{th}$, $G_m$, $SS$) at the end of the sweep.

---

## 6. Math Engine: Parameter Extraction

Once the sweep is completed, the math engine processes the data vectors:

### 6.1 Transconductance ($G_m$)
Computed using the central difference numerical derivative of $I_{ds}$ with respect to $V_{GS}$:

$$G_m[i] = \frac{I_{ds}[i+1] - I_{ds}[i-1]}{V_{GS}[i+1] - V_{GS}[i-1]}$$

To prevent derivative noise amplification, $I_{ds}$ is smoothed beforehand using a 5-point Savitzky-Golay filter (second-order polynomial, coefficients: $[-3, 12, 17, 12, -3] / 35$).

### 6.2 Threshold Voltage ($V_{th}$)
Calculated using the **linear extrapolation method of maximum transconductance**. At the peak transconductance point ($G_{m,max}$), a tangent line is projected onto the $V_{GS}$ axis where $I_{ds} = 0$:

$$V_{th} = V_{GS\_at\_Gm\_max} - \frac{I_{ds\_at\_Gm\_max}}{G_{m,max}}$$

### 6.3 Subthreshold Swing ($SS$)
Extracted from the subthreshold region using a sliding-window linear regression on the $\log_{10}(I_{ds}) \times V_{GS}$ plot:
1. Smooths $I_{ds}$ using a 3-point moving average.
2. Filters out data points where $I_{ds} \le 10^{-13}\text{ A}$.
3. Applies a sliding window (width varying from 5 to 20 points) to compute linear regression slope and $R^2$.
4. Discards windows where $I_{ds}$ is not strictly increasing or the total span is under $0.5$ decades.
5. Selects the window with the best correlation coefficient ($R^2 \ge 0.85$).
6. Calculates the swing:

$$SS = \frac{1}{\text{slope}} \times 1000\text{ [mV/decade]}$$
