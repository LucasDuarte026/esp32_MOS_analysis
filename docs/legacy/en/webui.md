# Web Interface & REST API

The user interface of the platform is designed with a "zero-install" philosophy. The ESP32 hosts an asynchronous web server that delivers a responsive dashboard constructed using vanilla HTML5, CSS3, and JavaScript, with no dependencies on heavy modern frameworks (like React or Vue), keeping the network payload minimal.

---

## 1. Frontend Structure & Design System

The frontend application resides in the `web/` folder and is separated into modular JavaScript files.

### 1.1 Code Modularization
* **`core.js`**: Contains utility functions, toast notifications (`showToast()`), and the custom logging wrapper.
* **`collection.js`**: Controls the configuration interface, handles hardware status checking, and manages the sweep state machine.
* **`visualization.js`**: Parses CSV files, renders interactive charts using Plotly.js, calculates curves, and draws tangent overlay lines.
* **`email.js`**: Handles SMTP export configurations and monitors direct email transmissions.

### 1.2 Custom Logging Wrapper
To prevent developer logs from polluting production runs, `console.log` is banned in the frontend code. Instead, the `dbg(category, message, ...)` function is used. Allowed categories are:
* `API`: REST requests and polling operations.
* `CSV`: Parser states and metadata readings.
* `PLOT`: Chart rendering events.
* `UI`: Modal openings and user inputs.
* `MATH`: Client-side calculations.

---

## 2. Web Pages Overview

The web dashboard is split into three main tabs or pages:

### 2.1 Coleta (Data Collection) — `/`
The landing page allows users to set up sweep parameters (range, steps, settling time, oversampling multiplier, and shunt values) and launch measurements.
* **Interactive Hardware Check**: Pings the I2C bus and displays a visual status grid (e.g., `MCP4725 VGS` ✅, `MCP4725 VDS` ❌). If a peripheral is missing, the sweep button is disabled and a diagnostic dialog prompts the user to verify wiring or switch to the internal ADC/DAC fallback mode.
* **Real-Time Progress Bar**: Polls the device status every 2 seconds, displaying the current target $V_{GS}$/$V_{DS}$ and progress percentage.

### 2.2 Visualização (Visualization & Analysis) — `/visualization`
Loads data stored on the ESP32 partition.
* **Plotly.js Charting**: Plots $I_{ds} \times V_{GS}$ (linear and logarithmic scales), $G_m$ curve, and $I_{ds} \times V_{DS}$ curves.
* **Tangent & Parameter Overlay**: Draws a projection line indicating the Subthreshold Swing slope and a marker showing the exact extracted Threshold Voltage ($V_{th}$).
* **CSV Download**: Provides a download link to copy the file directly to the client PC.

### 2.3 E-mail (SMTP Export) — `/email`
Allows exporting CSVs directly from the instrument. The user inputs their SMTP configuration, and the ESP32 compiles and transmits the email with the CSV attachment.

---

## 3. REST API Specifications

The firmware exposes several REST API endpoints over HTTP:

### 3.1 Device Control Endpoints

* **`GET /api/status`**
  * **Description**: Returns device state machine status, current configurations, and detected I2C hardware flags.
  * **Response Schema (JSON)**:
    ```json
    {
      "state": "STANDBY",
      "hardware": {
        "mcp4725_vgs": true,
        "mcp4725_vds": true,
        "ads1115": true
      }
    }
    ```

* **`POST /api/start`**
  * **Description**: Requests the start of a sweep with configuration parameters.
  * **Request Payload (JSON)**:
    ```json
    {
      "mode": 1,
      "shunt": 100.0,
      "vgs_start": 0.0,
      "vgs_end": 3.5,
      "vgs_step": 0.01,
      "vds_fixed": 0.1,
      "settling_ms": 100,
      "oversampling": 16
    }
    ```

* **`POST /api/cancel`**
  * **Description**: Aborts the active measurement loop, shuts down the DACs, and activates the bleeder.

* **`GET /api/progress`**
  * **Description**: Returns progress stats for active sweeps.
  * **Response Schema (JSON)**:
    ```json
    {
      "running": true,
      "progress": 45,
      "vgs_current": 1.575,
      "vds_current": 0.100,
      "message": "Measuring VGS = 1.575V"
    }
    ```

### 3.2 File System Endpoints

* **`GET /api/files`**
  * **Description**: Lists all saved CSV files on the FFat partition, including sizes.
* **`GET /api/files/download?file=filename.csv`**
  * **Description**: Serves the requested CSV file as a file download stream.
* **`DELETE /api/files/delete?file=filename.csv`**
  * **Description**: Deletes the selected file from the partition.

---

## 4. UI Compiling & Embedding Pipeline

Because SPIFFS/LittleFS partitions can be slow to read and prone to corruption if files are constantly rewritten alongside active sweeps, the web assets are compiled directly into the ESP32's flash memory (`PROGMEM`).

```
                    Web Asset Source Files (web/)
                       [HTML, CSS, JS, Assets]
                                 │
                                 ▼
                     PlatformIO pre-build hook
                     [scripts/embed_web.py]
                                 │
                                 ▼
                 Compressed Header Files (PROGMEM)
                 [src/generated/web_dashboard.h]
                                 │
                                 ▼
                        ESPAsyncWebServer
                     [sendProgmemChunked()]
```

### 4.1 How embed_web.py works
1. Scans the `/web` directory.
2. Compresses the CSS and JS files to strip comments and whitespace.
3. Generates hexadecimal byte arrays mapping the static files.
4. Generates a header file (`src/generated/web_dashboard.h`) containing arrays and size constants.

When a client requests `/` or static assets, the asynchronous server streams the arrays out of flash memory in chunks using `sendProgmemChunked()`. This maintains a zero-heap foot-print during asset delivery.
