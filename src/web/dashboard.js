// Global state
let currentVDD = 5.0; // Default VDD voltage
let usbConnected = false;
let currentCSVData = []; // Store full parsed CSV data
let uniqueVDSValues = []; // Store unique VDS values found in CSV
let currentSweepMode = 'VGS'; // Sweep mode from CSV header: 'VGS' or 'VDS'

// Multi-page architecture. No tab navigation here.

// Fetch system info and update display
async function updateSystemInfo() {
    try {
        const response = await fetch('/api/system_info');
        const data = await response.json();

        // Update temperature
        document.getElementById('temperature').textContent = `${data.temperature.toFixed(1)}°C`;

        // Update USB status
        usbConnected = data.usb_connected;
        document.getElementById('connection-status').textContent = data.usb_connected ? 'Serial USB Ativa ✓' : 'Inativa';
        document.getElementById('connection-status').style.color = data.usb_connected ? '#4CAF50' : '#F44336';

        // Update header status indicator
        const headerStatusText = document.getElementById('header-status-text');
        const headerStatusDot = document.getElementById('header-status-dot');
        if (headerStatusText && headerStatusDot) {
            headerStatusText.textContent = data.usb_connected ? 'Comunicação USB Ativa' : 'Apenas WiFi';
            headerStatusDot.style.background = data.usb_connected ? '#4CAF50' : '#FFA726';
        }

        // Update chip ID
        document.getElementById('sensor-id').textContent = data.chip_id;

        // Update Version (Dynamically add if missing)
        let versionEl = document.getElementById('fw-version');
        if (!versionEl && data.version) {
            const container = document.querySelector('#sensor-id').parentElement.parentElement;
            const row = document.createElement('div');
            row.className = 'info-row';
            row.innerHTML = `<span class="info-label">Versão FW</span><span class="info-value" id="fw-version" style="font-family: monospace;">${data.version}</span>`;
            container.appendChild(row);
        } else if (versionEl && data.version) {
            versionEl.textContent = data.version;
        }

        // Update free heap
        const heapKB = (data.free_heap / 1024).toFixed(1);
        document.getElementById('free-heap').textContent = `${heapKB} KB`;

        // Update Debug Mode status
        let debugEl = document.getElementById('debug-status');
        if (!debugEl) {
            // Create debug status row dynamically if not exists
            const container = document.querySelector('#free-heap').parentElement.parentElement;
            const row = document.createElement('div');
            row.className = 'info-row';
            row.innerHTML = `<span class="info-label">Debug Log <small style="color:#888">(GPIO12→GND)</small></span><span class="info-value" id="debug-status"></span>`;
            container.appendChild(row);
            debugEl = document.getElementById('debug-status');
        }
        if (debugEl) {
            if (data.debug_mode) {
                debugEl.innerHTML = `<span style="color:#4CAF50">✓ Ativo</span>`;
            } else {
                debugEl.innerHTML = `<span style="color:#F44336">✗ Inativo</span>`;
            }
        }

        // Update VDD if USB is connected
        if (data.usb_connected) {
            currentVDD = 5.0;
        }
    } catch (error) {
        console.error('Error fetching system info:', error);
    }
}

// Validate voltage limits
function validateVoltageLimit(inputElement) {
    const value = parseFloat(inputElement.value);
    if (value > currentVDD) {
        alert(`⚠️ Atenção: A tensão máxima é limitada pela alimentação VDD (${currentVDD}V${usbConnected ? ' via USB' : ''})`);
        inputElement.value = currentVDD.toFixed(1);
    }
}

// Add validation listeners to voltage inputs
['vds-start', 'vds-end', 'vgs-start', 'vgs-end'].forEach(id => {
    const input = document.getElementById(id);
    if (input) {
        input.addEventListener('change', () => validateVoltageLimit(input));
    }
});

// Clear logs button
document.getElementById('btn-clear-logs')?.addEventListener('click', () => {
    document.getElementById('logs-container').innerHTML = '';
});

// Reset fields button
document.getElementById('btn-reset-fields')?.addEventListener('click', () => {
    // VDS fields
    document.getElementById('vds-start').value = '0';
    document.getElementById('vds-end').value = '5.0';
    document.getElementById('vds-step').value = '0.05';

    // VGS fields
    document.getElementById('vgs-start').value = '0';
    document.getElementById('vgs-end').value = '3.5';
    document.getElementById('vgs-step').value = '0.05';

    // Hardware fields
    document.getElementById('rshunt').value = '';
    document.getElementById('settling-time').value = '5';
    document.getElementById('filename').value = '';
});

// Character count for email
document.getElementById('email-message')?.addEventListener('input', (e) => {
    document.getElementById('char-count').textContent = e.target.value.length;
});

// Sweep Mode Toggle - update visual state and description
document.getElementById('sweep-mode-toggle')?.addEventListener('change', (e) => {
    const vgsLabel = document.getElementById('mode-vgs-label');
    const vdsLabel = document.getElementById('mode-vds-label');
    const descEl = document.getElementById('sweep-mode-desc');

    if (e.target.checked) {
        // VDS mode (Curva Id x Vds)
        vgsLabel.classList.remove('mode-active');
        vgsLabel.classList.add('mode-dimmed');
        vdsLabel.classList.remove('mode-dimmed');
        vdsLabel.classList.add('mode-active');
        if (descEl) descEl.textContent = 'Varia VDS para cada VGS fixo';
    } else {
        // VGS mode (Curva Id x Vgs) - default
        vgsLabel.classList.add('mode-active');
        vgsLabel.classList.remove('mode-dimmed');
        vdsLabel.classList.add('mode-dimmed');
        vdsLabel.classList.remove('mode-active');
        if (descEl) descEl.textContent = 'Varia VGS para cada VDS fixo (padrão)';
    }
});

// Visible curves state (for toggle buttons)
let visibleCurves = {
    ids: true,
    gm: false,
    ss: false,
    vt: false
};

// Toggle buttons for visualization - properly update curve visibility
document.querySelectorAll('.toggle-btn[data-curve]').forEach(btn => {
    btn.addEventListener('click', () => {
        const curveType = btn.dataset.curve;
        btn.classList.toggle('active');
        visibleCurves[curveType] = btn.classList.contains('active');
        updatePlotsMultiCurve();
    });
});

// Load available measurements from ESP32 - Fixed to preserve selection
let currentSelectedFile = ''; // Track selection to prevent reset

async function loadMeasurementList() {
    const select = document.getElementById('file-select');
    if (!select) return;

    // Remember current selection
    const previousSelection = currentSelectedFile || select.value;

    try {
        const response = await fetch(`/api/files?t=${Date.now()}`);
        const data = await response.json();

        select.innerHTML = '<option value="">-- Selecione uma medida --</option>';

        const sortedFiles = data.files.slice().reverse();
        sortedFiles.forEach(file => {
            const option = document.createElement('option');
            option.value = file.name;
            const date = new Date(file.timestamp * 1000).toLocaleString('pt-BR');
            option.textContent = `${file.name.replace('.csv', '')} (${date})`;
            select.appendChild(option);
        });

        // Restore previous selection if it still exists
        if (previousSelection && [...select.options].some(opt => opt.value === previousSelection)) {
            select.value = previousSelection;
        }

        if (data.warning) {
            console.warn(`⚠️ ${data.count}/200 arquivos armazenados`);
        }
    } catch (error) {
        console.error('Error loading measurements:', error);
    }
}


// Start measurement button with Real Polling (and Cancel support)
document.getElementById('btn-start-collection')?.addEventListener('click', async (e) => {
    const btn = e.currentTarget;

    // Check if we are in cancel mode
    if (btn.dataset.action === 'cancel') {
        if (!confirm('Deseja cancelar a medição atual? O arquivo será EXCLUÍDO.')) return;

        try {
            btn.disabled = true;
            btn.textContent = "Cancelando...";
            await fetch('/api/cancel', { method: 'POST' });
            showToast("Cancelando medição...", "warning");
        } catch (error) {
            showToast("Erro ao cancelar: " + error.message, "error");
            btn.disabled = false;
        }
        return;
    }

    // Get form values
    const vgsStart = parseFloat(document.getElementById('vgs-start').value);
    const vgsEnd = parseFloat(document.getElementById('vgs-end').value);
    const vgsStep = parseFloat(document.getElementById('vgs-step').value);
    const vdsStart = parseFloat(document.getElementById('vds-start').value);
    const vdsEnd = parseFloat(document.getElementById('vds-end').value);
    const vdsStep = parseFloat(document.getElementById('vds-step').value);
    const rshunt = parseFloat(document.getElementById('rshunt').value);
    const settlingTime = parseInt(document.getElementById('settling-time').value);
    const filename = document.getElementById('filename').value;

    // Validate inputs
    let errorMsg = '';

    // Filename Validation (Strict)
    const validFilenameRegex = /^[a-zA-Z0-9_\-\.]+$/;
    if (filename && !validFilenameRegex.test(filename)) {
        errorMsg += '- Nome do arquivo inválido (use apenas letras, números, _, - e .)\n';
    }

    // VGS Validations
    if (isNaN(vgsStart) || isNaN(vgsEnd) || isNaN(vgsStep)) errorMsg += '- Parâmetros VGS inválidos\n';
    else if (vgsStep <= 0) errorMsg += '- Passo VGS deve ser positivo\n';

    // VDS Validations
    if (isNaN(vdsStart) || isNaN(vdsEnd) || isNaN(vdsStep)) errorMsg += '- Parâmetros VDS inválidos\n';
    else if (vdsStep <= 0) errorMsg += '- Passo VDS deve ser positivo\n';

    // Hardware Validations
    if (isNaN(rshunt) || rshunt <= 0) errorMsg += '- Resistor Shunt inválido\n';

    if (errorMsg) {
        alert('⚠️ Erro na configuração:\n' + errorMsg);
        return;
    }


    // Get sweep mode from toggle
    const sweepModeToggle = document.getElementById('sweep-mode-toggle');
    const sweepMode = sweepModeToggle && sweepModeToggle.checked ? 'VDS' : 'VGS';

    const config = {
        vgs_start: vgsStart,
        vgs_end: vgsEnd,
        vgs_step: vgsStep,
        vds_start: vdsStart,
        vds_end: vdsEnd,
        vds_step: vdsStep,
        rshunt: rshunt,
        settling_ms: settlingTime || 5,
        filename: filename || 'mosfet_data.csv',
        sweep_mode: sweepMode,
        timestamp: Math.floor(Date.now() / 1000)
    };

    try {
        // Disable button
        const btn = document.getElementById('btn-start-collection');
        btn.disabled = true;
        btn.innerHTML = '<span class="status-dot" style="background:white;width:8px;height:8px;"></span> Inicializando...';

        const progressSection = document.getElementById('progress-section');
        if (progressSection) {
            progressSection.style.display = 'block';
            document.getElementById('progress-text').textContent = "Iniciando...";
            document.getElementById('progress-fill').style.width = '0%';
        }

        const response = await fetch('/api/start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });

        if (response.status === 202) { // Accepted/Started
            // Start Polling
            pollProgress();
        } else {
            const result = await response.json();
            throw new Error(result.error || 'Falha ao iniciar');
        }

    } catch (error) {
        showToast(`❌ Falha: ${error.message}`, "error");
        resetCollectionButton();
    }
});

// Poll Progress Function
async function pollProgress() {
    try {
        const response = await fetch('/api/progress');
        const data = await response.json();

        const progressSection = document.getElementById('progress-section');
        if (progressSection) {
            document.getElementById('progress-text').textContent = data.message || "Coletando...";
            document.getElementById('progress-percent').textContent = `${data.progress}%`;
            document.getElementById('progress-fill').style.width = `${data.progress}%`;
        }

        // Update button text with VDS if available
        const btn = document.getElementById('btn-start-collection');
        if (btn && data.running) {
            btn.disabled = false; // Make sure it's clickable for cancel
            btn.dataset.action = 'cancel'; // Set action flag
            btn.style.backgroundColor = '#f44336'; // Red color
            btn.innerHTML = `
                <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
                    <path d="M19 6.41L17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12z"/>
                </svg>
                Cancelar (${data.progress}%)
            `;
        }

        if (data.running) {
            // Keep polling
            setTimeout(pollProgress, 500);
        } else {
            // Finished - check for errors first
            if (data.error && data.error_msg) {
                showToast("❌ ERRO: " + data.error_msg, "error");
                if (progressSection) {
                    document.getElementById('progress-text').textContent = "ERRO: " + data.error_msg;
                    document.getElementById('progress-fill').style.backgroundColor = '#f44336';
                }
            } else if (data.progress >= 100) {
                showToast("✅ Medição concluída!", "success");
                if (progressSection) document.getElementById('progress-text').textContent = "Concluído!";
            } else {
                showToast("⚠️ Medição finalizada", "warning");
            }
            resetCollectionButton();
        }
    } catch (e) {
        console.error("Polling error", e);
        setTimeout(pollProgress, 2000); // Retry slower
    }
}

function resetCollectionButton() {
    const btn = document.getElementById('btn-start-collection');
    if (btn) {
        btn.disabled = false;
        btn.dataset.action = ''; // Clear cancel action
        btn.style.backgroundColor = ''; // Reset color
        btn.innerHTML = `
            <svg width="16" height="16" viewBox="0 0 24 24" fill="currentColor">
                <path d="M8 5v14l11-7z" />
            </svg>
            Iniciar Coleta
        `;
    }
}

// Helper for Toast Notifications
function showToast(message, type = 'info') {
    const container = document.getElementById('toast-container');
    if (!container) {
        alert(message);
        return;
    }

    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    toast.style.cssText = `
        padding: 12px 24px;
        margin-bottom: 10px;
        border-radius: 4px;
        color: white;
        font-weight: 500;
        box-shadow: 0 4px 12px rgba(0,0,0,0.2);
        animation: slideIn 0.3s ease;
        background: ${type === 'error' ? '#f44336' : (type === 'success' ? '#4caf50' : '#2196f3')};
    `;

    toast.textContent = message;
    container.appendChild(toast);

    setTimeout(() => {
        toast.style.animation = 'slideOut 0.3s ease forwards';
        setTimeout(() => toast.remove(), 300);
    }, 4000);
}

// Add CSS keyframes for toast via JS if not present
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn { from { transform: translateX(100%); opacity: 0; } to { transform: translateX(0); opacity: 1; } }
    @keyframes slideOut { to { transform: translateX(100%); opacity: 0; } }
`;
document.head.appendChild(style);

// File selector for visualization tab
document.getElementById('file-select')?.addEventListener('change', async (e) => {
    const downloadBtn = document.getElementById('btn-download-measurement');
    const deleteBtn = document.getElementById('btn-delete-measurement');
    const vdsSelect = document.getElementById('vds-select');
    const selectedFile = e.target.value;

    // Save current selection to prevent reset bug
    currentSelectedFile = selectedFile;

    if (!selectedFile || selectedFile === '') {
        if (downloadBtn) downloadBtn.disabled = true;
        if (deleteBtn) deleteBtn.disabled = true;
        vdsSelect.disabled = true;
        vdsSelect.innerHTML = '<option value="">Selecione um arquivo primeiro...</option>';
        currentCSVData = [];
        return;
    }

    if (downloadBtn) downloadBtn.disabled = false;
    if (deleteBtn) deleteBtn.disabled = false;
    vdsSelect.disabled = true;
    vdsSelect.innerHTML = '<option value="">Carregando dados...</option>';

    // Load CSV Data
    try {
        const response = await fetch(`/api/files/download?file=${encodeURIComponent(selectedFile)}&t=${Date.now()}`);
        if (!response.ok) throw new Error('Falha ao baixar arquivo');

        const csvText = await response.text();
        parseCSV(csvText);

        // Update curve selector label based on sweep mode
        const curveLabel = document.getElementById('curve-select-label');
        if (curveLabel) {
            if (currentSweepMode === 'VDS') {
                curveLabel.textContent = 'Selecionar Curva VGS:';
            } else {
                curveLabel.textContent = 'Selecionar Curva VDS:';
            }
        }

        // Populate curve Select with values from CSV (no 'all' option)
        vdsSelect.innerHTML = '';
        uniqueVDSValues.forEach((val, idx) => {
            const option = document.createElement('option');
            option.value = val;
            if (currentSweepMode === 'VDS') {
                option.textContent = `VGS = ${val.toFixed(3)} V`;
            } else {
                option.textContent = `VDS = ${val.toFixed(3)} V`;
            }
            vdsSelect.appendChild(option);
        });

        // Select first value by default
        if (uniqueVDSValues.length > 0) {
            vdsSelect.value = uniqueVDSValues[0];
        }


        vdsSelect.disabled = false;
        updatePlotsMultiCurve(); // Use multi-curve plot function

    } catch (error) {
        console.error("Error loading CSV:", error);
        vdsSelect.innerHTML = '<option value="">Erro ao carregar</option>';
        showToast("Erro ao carregar arquivo de dados.", "error");
    }
});


// Initialize Metric Selector
(function initMetricSelector() {
    const controls = document.querySelector('.viz-controls');
    if (controls && !document.getElementById('plot-type-select')) {
        const select = document.createElement('select');
        select.id = 'plot-type-select';
        select.className = 'control-input';
        select.innerHTML = `
            <option value="ids">Corrente (Ids)</option>
            <option value="gm">Transcondutância (Gm)</option>
            <option value="ss">Subthreshold Swing (SS)</option>
            <option value="vsh">Tensão Shunt (Vsh)</option>
        `;
        select.addEventListener('change', updatePlots);

        // Insert before existing file select or append
        controls.appendChild(select);
        console.log("Metric selector injected");
    }
})();

// CSV Parser
// CSV Parser
// CSV Parser
function parseCSV(csvText) {
    console.log(`[DEBUG] Parsing CSV: ${csvText.length} bytes`);
    if (csvText.length < 10) {
        console.warn("[DEBUG] CSV too short/empty");
        logErrorAndAlert("Erro: Arquivo vazio ou inválido recebido.");
        return;
    }

    currentCSVData = [];
    uniqueVDSValues = [];

    const lines = csvText.trim().split('\n');
    let dataStartIndex = -1;
    const analysisMap = {};

    console.log(`[DEBUG] Total lines: ${lines.length}`);

    // 1. Scan for header and metadata first
    for (let i = 0; i < lines.length; i++) { // Scan all lines, header might be anywhere
        const line = lines[i].trim();

        // Detect column header line (timestamp,vds...)
        if (line.includes('timestamp,vds') || line.includes('time,vds')) {
            dataStartIndex = i + 1;
            console.log(`[DEBUG] Found header at line ${i}: ${line}`);
        }

        // DETECT SWEEP MODE from header: "# Sweep Mode: VGS" or "# Sweep Mode: VDS"
        if (line.startsWith('# Sweep Mode:')) {
            const modeMatch = line.match(/# Sweep Mode:\s*(VGS|VDS)/i);
            if (modeMatch) {
                currentSweepMode = modeMatch[1].toUpperCase();
                console.log(`[DEBUG] Detected sweep mode: ${currentSweepMode}`);
            }
        }

        // Parse Analysis Metadata: "# VDS=0.50V: Vt=1.200V, SS=85.00 mV/dec, MaxGm=1.200e-03 S"
        if (line.startsWith('# VDS=')) {
            try {
                const vdsMatch = line.match(/VDS=([\d\.]+)V/);
                const vtMatch = line.match(/Vt=([\d\.]+)V/);
                const ssMatch = line.match(/SS=([0-9\.]+)\s?mV\/dec/); // flexible space
                const gmMatch = line.match(/MaxGm=([0-9\.eE\-\+]+)\s?S/);

                if (vdsMatch) {
                    const vdsVal = parseFloat(vdsMatch[1]);
                    const vdsKey = Math.round(vdsVal * 1000) / 1000;
                    analysisMap[vdsKey] = {
                        vt: vtMatch ? parseFloat(vtMatch[1]) : 0,
                        ss: ssMatch ? parseFloat(ssMatch[1]) : 0,
                        max_gm: gmMatch ? parseFloat(gmMatch[1]) : 0
                    };
                    // console.log(`[DEBUG] Meta VDS=${vdsKey}`, analysisMap[vdsKey]);
                }
            } catch (e) {
                console.warn("Mdata parse err:", line);
            }
        }
    }

    // Fallback: If no header found, guess
    if (dataStartIndex === -1) {
        console.warn("[DEBUG] No header found. Guessing start index...");
        for (let i = 0; i < lines.length; i++) {
            // First line that is NOT a comment and has commas is likely data
            if (!lines[i].trim().startsWith('#') && lines[i].includes(',')) {
                dataStartIndex = i;
                console.log(`[DEBUG] Guessed start index: ${i}`);
                break;
            }
        }
        if (dataStartIndex === -1) dataStartIndex = 0;
    }

    const vdsSet = new Set();
    let rowErrorCount = 0;

    for (let i = dataStartIndex; i < lines.length; i++) {
        const parts = lines[i].split(',');
        if (parts.length < 5) {
            // console.warn(`[DEBUG] Skipping short line ${i}: ${lines[i]}`);
            continue;
        }

        // Enhanced Format: timestamp,vds,vgs,vsh,ids,gm
        // Legacy Format: timestamp,vds,vgs,vsh,ids,gm,vt,ss

        const vds = parseFloat(parts[1]);
        const vgs = parseFloat(parts[2]);
        const vsh = parseFloat(parts[3]);
        const ids = parseFloat(parts[4]);
        const gm = parseFloat(parts[5]);

        // Legacy fallback
        let vt = (parts.length > 6) ? parseFloat(parts[6]) : 0;
        let ss = (parts.length > 7) ? parseFloat(parts[7]) : 0;

        if (!isNaN(vds) && !isNaN(vgs)) {
            const vdsRounded = Math.round(vds * 1000) / 1000;
            vdsSet.add(vdsRounded);

            // If we have metadata from header, override Vt/SS
            if (analysisMap[vdsRounded]) {
                vt = analysisMap[vdsRounded].vt;
                ss = analysisMap[vdsRounded].ss;
            }

            currentCSVData.push({
                vds: vdsRounded,
                vgs: vgs,
                vsh: vsh,
                ids: isNaN(ids) ? 0 : ids,
                gm: isNaN(gm) ? 0 : gm, // Will be recalculated if 0
                vt: vt,
                ss: ss,
                max_gm: analysisMap[vdsRounded] ? analysisMap[vdsRounded].max_gm : 0
            });
        } else {
            rowErrorCount++;
        }
    }

    // Sort VDS values
    uniqueVDSValues = Array.from(vdsSet).sort((a, b) => a - b);

    // Recalculate Gm if missing (since backend streaming doesn't include it per point)
    calculateGmForData(currentCSVData, currentSweepMode);
    calculateSSForData(currentCSVData); // Recalculate SS for frontend visualization

    console.log(`[DEBUG] Parsed ${currentCSVData.length} valid points. Unique VDS:`, uniqueVDSValues);

    if (currentCSVData.length === 0) {
        console.error("[DEBUG] No valid data points found!");
        logErrorAndAlert("Erro: Nenhum dado válido encontrado no arquivo CSV.");
    }
}

// Helper to show errors in UI and Alert
function logErrorAndAlert(msg) {
    showToast(msg, 'error');
    console.error('[CSV ERROR] ' + msg);
    const logContent = document.getElementById('log-content');
    if (logContent) {
        const entry = document.createElement('div');
        entry.className = 'log-entry log-error';
        entry.style.color = '#ff4444';
        entry.textContent = '[ERRO] ' + msg;
        logContent.insertBefore(entry, logContent.firstChild);
    }
}

// Helper to calculate Gm (dIds/dVgs) if missing or 0
function calculateGmForData(data, sweepMode) {
    if (!data || data.length < 2) return;

    // Group by curve (VDS in VGS mode, VGS in VDS mode)
    const curves = {};
    data.forEach(d => {
        const key = sweepMode === 'VDS' ? d.vgs : d.vds;
        if (!curves[key]) curves[key] = [];
        curves[key].push(d);
    });

    // Calculate Gm for each curve
    Object.keys(curves).forEach(k => {
        const curve = curves[k];
        // Sort by X-axis (VGS in VGS mode, VDS in VDS mode)
        curve.sort((a, b) => (sweepMode === 'VDS' ? a.vds - b.vds : a.vgs - b.vgs));

        for (let i = 1; i < curve.length - 1; i++) {
            // Central difference
            const prev = curve[i - 1];
            const next = curve[i + 1];

            const dx = sweepMode === 'VDS' ? (next.vds - prev.vds) : (next.vgs - prev.vgs);
            const dy = next.ids - prev.ids;

            if (Math.abs(dx) > 1e-6) {
                curve[i].gm = dy / dx;
            }
        }
    });
}

// Helper to calculate SS (Subthreshold Swing) using derivative-based detection
// SS = dVgs / d(log10(Ids)) in the subthreshold region
// Method: Find region where d(log10(Ids))/dVgs is consistent (exponential behavior)
// This approach doesn't rely on absolute current values - only on the slope consistency
function calculateSSForData(data) {
    if (!data || data.length < 5) return;

    // Group by VDS curve
    const curves = {};
    data.forEach(d => {
        const key = d.vds;
        if (!curves[key]) curves[key] = [];
        curves[key].push(d);
    });

    Object.keys(curves).forEach(vdsKey => {
        const curve = curves[vdsKey];
        curve.sort((a, b) => a.vgs - b.vgs);

        // STEP 1: Calculate d(log10(Ids))/dVgs for each point
        // This is the inverse of SS: large values = steep slope = subthreshold region
        const slopes = [];
        for (let i = 1; i < curve.length - 1; i++) {
            const prev = curve[i - 1];
            const curr = curve[i];
            const next = curve[i + 1];

            const ids_prev = Math.abs(prev.ids);
            const ids_next = Math.abs(next.ids);

            // Skip if current is too small (noise floor)
            if (ids_prev < 1e-12 || ids_next < 1e-12) {
                slopes.push({ idx: i, slope: 0, valid: false });
                continue;
            }

            const dVgs = next.vgs - prev.vgs;
            const dLogIds = Math.log10(ids_next) - Math.log10(ids_prev);

            if (Math.abs(dVgs) > 1e-9 && dLogIds > 0.01) {
                // Positive slope means current is increasing (subthreshold region)
                const invSS = dLogIds / dVgs; // decades/V - inverse of SS
                slopes.push({ idx: i, slope: invSS, dLogIds, dVgs, valid: true });
            } else {
                slopes.push({ idx: i, slope: 0, valid: false });
            }
        }

        // STEP 2: Find the region with consistent positive slope (exponential region)
        // Look for consecutive points with similar slope values
        const validSlopes = slopes.filter(s => s.valid && s.slope > 0.5); // At least 0.5 dec/V

        if (validSlopes.length < 3) {
            // Not enough exponential points found
            return;
        }

        // STEP 3: Find longest run of consistent slopes
        // Slopes should be within 50% of each other to be "consistent"
        let bestRun = [];
        let currentRun = [validSlopes[0]];

        for (let i = 1; i < validSlopes.length; i++) {
            const prevSlope = currentRun[currentRun.length - 1].slope;
            const currSlope = validSlopes[i].slope;
            const ratio = currSlope / prevSlope;

            // Check if slopes are consistent (within 2x of each other)
            if (ratio > 0.5 && ratio < 2.0) {
                currentRun.push(validSlopes[i]);
            } else {
                if (currentRun.length > bestRun.length) {
                    bestRun = currentRun;
                }
                currentRun = [validSlopes[i]];
            }
        }
        if (currentRun.length > bestRun.length) {
            bestRun = currentRun;
        }

        if (bestRun.length < 3) {
            return; // Not enough consistent exponential points
        }

        // STEP 4: Calculate SS from the best run using regression
        const subthresholdPoints = bestRun.map(s => curve[s.idx]);
        const ss = calculateSSByRegression(subthresholdPoints);

        if (ss > 0 && ss < 2000) { // Valid SS range: 0 to 2000 mV/dec
            curve.forEach(p => p.ss = ss);
        }

        // Also store instant SS for each point (for plotting)
        for (let i = 1; i < curve.length - 1; i++) {
            const prev = curve[i - 1];
            const curr = curve[i];
            const next = curve[i + 1];

            const ids_prev = Math.abs(prev.ids);
            const ids_next = Math.abs(next.ids);

            if (ids_prev > 1e-12 && ids_next > 1e-12) {
                const dVgs = next.vgs - prev.vgs;
                const dLogIds = Math.log10(ids_next) - Math.log10(ids_prev);

                if (Math.abs(dLogIds) > 0.01) { // At least 0.01 decade change
                    curr.ss_instant = (dVgs / dLogIds) * 1000; // mV/dec
                }
            }
        }
    });
}

// Estimate Vt from Gm peak (simple method)
function estimateVt(curve) {
    let maxGm = 0;
    let vtEstimate = 0;

    for (let i = 1; i < curve.length - 1; i++) {
        const gm = Math.abs(curve[i].gm || 0);
        if (gm > maxGm) {
            maxGm = gm;
            vtEstimate = curve[i].vgs;
        }
    }

    // Vt is typically slightly below the Gm peak
    return vtEstimate > 0 ? vtEstimate - 0.1 : 1.0;
}

// Find region where Ids grows exponentially with Vgs
function findExponentialRegion(curve) {
    const points = [];

    for (let i = 2; i < curve.length - 2; i++) {
        const ids = Math.abs(curve[i].ids);
        const ids_prev = Math.abs(curve[i - 1].ids);
        const ids_next = Math.abs(curve[i + 1].ids);

        // Check for exponential growth: ratio should be relatively constant
        if (ids > 1e-11 && ids < 1e-5 && ids_prev > 1e-12 && ids_next > 1e-12) {
            const ratio1 = ids / ids_prev;
            const ratio2 = ids_next / ids;

            // Both ratios should be > 1 (growing) and similar magnitude
            if (ratio1 > 1.1 && ratio2 > 1.1 && ratio1 < 100 && ratio2 < 100) {
                points.push(curve[i]);
            }
        }
    }

    return points;
}

// Calculate SS using linear regression: Vgs vs log10(Ids)
// Returns SS in mV/decade
function calculateSSByRegression(points) {
    if (points.length < 2) return 0;

    // Prepare data: x = log10(Ids), y = Vgs
    const x = [];
    const y = [];

    points.forEach(p => {
        const ids = Math.abs(p.ids);
        if (ids > 1e-15) {
            x.push(Math.log10(ids));
            y.push(p.vgs);
        }
    });

    if (x.length < 2) return 0;

    // Linear regression: y = mx + b
    // SS = m (slope) in V/decade, convert to mV/decade
    const n = x.length;
    const sumX = x.reduce((a, b) => a + b, 0);
    const sumY = y.reduce((a, b) => a + b, 0);
    const sumXY = x.reduce((acc, xi, i) => acc + xi * y[i], 0);
    const sumX2 = x.reduce((acc, xi) => acc + xi * xi, 0);

    const denominator = n * sumX2 - sumX * sumX;
    if (Math.abs(denominator) < 1e-10) return 0;

    const slope = (n * sumXY - sumX * sumY) / denominator; // V/decade

    // SS should be positive (Vgs increases with log(Ids) in subthreshold)
    const ss = slope * 1000; // Convert to mV/decade

    return ss > 0 ? ss : 0;
}


// Multi-curve plot function that respects toggle buttons
function updatePlotsMultiCurve() {
    const vdsSelect = document.getElementById('vds-select');
    const selectedVDS = vdsSelect ? vdsSelect.value : null;

    if (currentCSVData.length === 0) return;

    const traces = [];
    const colors = {
        ids: '#2196F3',  // Blue
        gm: '#FF9800',   // Orange
        ss: '#F44336',   // Red
        vt: '#4CAF50'    // Green
    };

    // Get data for selected curve value
    const curveVal = parseFloat(selectedVDS) || (uniqueVDSValues.length > 0 ? uniqueVDSValues[0] : 0);

    let plotData;
    if (currentSweepMode === 'VDS') {
        plotData = currentCSVData.filter(d => Math.abs(d.vgs - curveVal) < 0.001);
        plotData.sort((a, b) => a.vds - b.vds);
    } else {
        plotData = currentCSVData.filter(d => Math.abs(d.vds - curveVal) < 0.001);
        plotData.sort((a, b) => a.vgs - b.vgs);
    }

    if (plotData.length === 0) return;

    const xData = currentSweepMode === 'VDS' ? plotData.map(d => d.vds) : plotData.map(d => d.vgs);

    // Disable incompatible toggles in VDS mode
    const toggleGm = document.getElementById('toggle-gm');
    const toggleSs = document.getElementById('toggle-ss');
    const toggleVt = document.getElementById('toggle-vt');

    if (currentSweepMode === 'VDS') {
        if (toggleGm) { toggleGm.disabled = true; toggleGm.style.opacity = "0.3"; toggleGm.style.pointerEvents = "none"; }
        if (toggleSs) { toggleSs.disabled = true; toggleSs.style.opacity = "0.3"; toggleSs.style.pointerEvents = "none"; }
        if (toggleVt) { toggleVt.disabled = true; toggleVt.style.opacity = "0.3"; toggleVt.style.pointerEvents = "none"; }
        visibleCurves.gm = false;
        visibleCurves.ss = false;
        visibleCurves.vt = false;
        toggleGm?.classList.remove('active');
        toggleSs?.classList.remove('active');
        toggleVt?.classList.remove('active');
    } else {
        if (toggleGm) { toggleGm.disabled = false; toggleGm.style.opacity = "1"; toggleGm.style.pointerEvents = "auto"; }
        if (toggleSs) { toggleSs.disabled = false; toggleSs.style.opacity = "1"; toggleSs.style.pointerEvents = "auto"; }
        if (toggleVt) { toggleVt.disabled = false; toggleVt.style.opacity = "1"; toggleVt.style.pointerEvents = "auto"; }
    }

    // Trace 1: Ids
    if (visibleCurves.ids) {
        traces.push({
            x: xData,
            y: plotData.map(d => d.ids),
            mode: 'lines+markers',
            type: 'scatter',
            name: 'Ids (A)',
            yaxis: 'y',
            line: { color: colors.ids, shape: 'spline' },
            marker: { size: 4 }
        });
    }

    // Trace 2: Gm
    if (visibleCurves.gm) {
        traces.push({
            x: xData,
            y: plotData.map(d => d.gm),
            mode: 'lines+markers',
            type: 'scatter',
            name: 'Gm (S)',
            yaxis: 'y2',
            line: { color: colors.gm, shape: 'spline' },
            marker: { size: 4 }
        });
    }

    // Trace 3: SS (Instantaneous)
    if (visibleCurves.ss) {
        // Filter out undefined SS points
        const validSS = plotData.map((d, i) => ({ x: xData[i], y: d.ss_instant }));
        const xSS = validSS.filter(p => p.y && p.y < 5000).map(p => p.x); // Filter huge spikes
        const ySS = validSS.filter(p => p.y && p.y < 5000).map(p => p.y);

        if (ySS.length > 0) {
            traces.push({
                x: xSS,
                y: ySS,
                mode: 'lines+markers',
                type: 'scatter',
                name: 'SS (mV/dec)',
                yaxis: 'y3', // Separate axis for SS
                line: { color: colors.ss, shape: 'spline', dash: 'dot' },
                marker: { size: 3 }
            });
        }
    }

    // Vt and SS are now scalar values per curve, so we visualize them differently
    const vtValue = plotData.length > 0 ? plotData[0].vt : 0;
    const ssValue = plotData.length > 0 ? plotData[0].ss : 0;
    const maxGmValue = plotData.length > 0 ? plotData[0].max_gm : 0;

    // Count active secondary axes (only Gm is a trace now)
    const secondaryAxes = [visibleCurves.gm].filter(v => v).length;

    // Calculate right margin
    const rightMargin = 60 + (secondaryAxes * 50);
    const xDomainEnd = secondaryAxes > 0 ? 1 - (secondaryAxes * 0.08) : 1;

    // Construct Title with Analysis Results - use dynamic label based on sweep mode
    const curveLabel = currentSweepMode === 'VDS' ? 'VGS' : 'VDS';
    const curveValue = parseFloat(selectedVDS || uniqueVDSValues[0] || 0).toFixed(3);
    let titleText = `Curvas MOSFET (${curveLabel} = ${curveValue} V)`;
    titleText += `<br><span style="font-size: 12px; color: #ccc;">`;
    titleText += `Vt: <b>${vtValue.toFixed(3)} V</b> | `;
    titleText += `SS: <b>${ssValue.toFixed(3)} mV/dec</b> | `;
    titleText += `MaxGm: <b>${maxGmValue.toExponential(2)} S</b>`;
    titleText += `</span>`;

    // X-axis label depends on sweep mode
    const xAxisLabel = currentSweepMode === 'VDS' ? 'VDS (V)' : 'VGS (V)';

    const layout = {
        title: {
            text: titleText,
            font: { color: '#ffffff' }
        },
        paper_bgcolor: 'rgba(0,0,0,0)',
        plot_bgcolor: 'rgba(255,255,255,0.02)',
        font: { color: '#bdbdbd' },

        xaxis: {
            title: xAxisLabel,
            domain: [0, xDomainEnd],
            gridcolor: '#333',
            zerolinecolor: '#666'
        },
        yaxis: {
            title: 'Ids (A)',
            titlefont: { color: colors.ids },
            tickfont: { color: colors.ids },
            side: 'left',
            gridcolor: '#333'
        },
        yaxis2: {
            title: 'Gm (S)',
            titlefont: { color: colors.gm },
            tickfont: { color: colors.gm },
            anchor: 'free',
            overlaying: 'y',
            side: 'right',
            position: xDomainEnd,
            showgrid: false
        },
        yaxis3: {
            title: 'SS (mV/dec)',
            titlefont: { color: colors.ss },
            tickfont: { color: colors.ss },
            anchor: 'free',
            overlaying: 'y',
            side: 'right',
            position: xDomainEnd + 0.08, // Offset to right of Gm
            showgrid: false,
            // Only show if SS visible
            visible: visibleCurves.ss
        },
        margin: { t: 80, r: rightMargin, l: 60, b: 60 },
        showlegend: true,
        legend: { x: 0, y: 1.1, orientation: 'h', font: { color: '#e0e0e0' } },

        shapes: [],
        annotations: []
    };

    // Add Vt Line if visible
    if (visibleCurves.vt && vtValue > 0) {
        layout.shapes.push({
            type: 'line',
            x0: vtValue,
            x1: vtValue,
            y0: 0,
            y1: 1,
            xref: 'x',
            yref: 'paper',
            line: {
                color: colors.vt,
                width: 2,
                dash: 'dash'
            }
        });
        layout.annotations.push({
            x: vtValue,
            y: 0,
            xref: 'x',
            yref: 'paper',
            text: `Vt`,
            showarrow: false,
            yshift: 10,
            font: { color: colors.vt, size: 12 }
        });
    }
    let plotDiv = document.getElementById('plot-container');
    if (!plotDiv) {
        console.warn("No chart container found (plot-container)");
        return;
    }

    if (window.Plotly) {
        Plotly.newPlot(plotDiv, traces, layout, { responsive: true });
    } else {
        plotDiv.textContent = "Erro: Biblioteca Plotly não carregada.";
    }

    // Update metrics cards
    updateMetrics(plotData);
}

// Update metric cards with calculated values
// Update metric cards with calculated values
function updateMetrics(plotData) {
    const vtEl = document.getElementById('metric-vt');
    const gmEl = document.getElementById('metric-gm');
    const ssEl = document.getElementById('metric-ss');

    if (currentSweepMode === 'VDS') {
        const msg = "Não contém em Curva VDS";
        if (vtEl) {
            vtEl.textContent = msg;
            vtEl.style.fontSize = "14px"; // Adjust font size for longer text
        }
        if (gmEl) {
            gmEl.textContent = msg;
            gmEl.style.fontSize = "14px";
        }
        if (ssEl) {
            ssEl.textContent = msg;
            ssEl.style.fontSize = "14px";
        }
        return;
    }

    if (!plotData || plotData.length === 0) return;

    // Reset font size
    if (vtEl) vtEl.style.fontSize = "";
    if (gmEl) gmEl.style.fontSize = "";
    if (ssEl) ssEl.style.fontSize = "";

    // Find max Gm and corresponding Vt
    let maxGm = 0;
    let vtAtMaxGm = 0;
    let avgSS = 0;
    let ssCount = 0;

    plotData.forEach(d => {
        if (d.gm > maxGm) {
            maxGm = d.gm;
            vtAtMaxGm = d.vt;
        }
        if (d.ss > 0 && d.ss < 1000) { // Filter unreasonable SS values
            avgSS += d.ss;
            ssCount++;
        }
    });

    avgSS = ssCount > 0 ? avgSS / ssCount : 0;

    if (vtEl) vtEl.textContent = `${vtAtMaxGm.toFixed(3)} V`;
    if (gmEl) gmEl.textContent = `${(maxGm * 1000).toFixed(3)} mS`;
    if (ssEl) ssEl.textContent = `${avgSS.toFixed(3)} mV/dec`;
}

// Listen for VDS changes
document.getElementById('vds-select')?.addEventListener('change', updatePlotsMultiCurve);


// Fix #10: Download measurement button - now downloads the SELECTED file
document.getElementById('btn-download-measurement')?.addEventListener('click', async () => {
    const selectedValue = document.getElementById('file-select').value;

    if (!selectedValue || selectedValue === '') {
        alert('⚠️ Selecione uma medida primeiro');
        return;
    }

    try {
        const response = await fetch(`/api/files/download?file=${encodeURIComponent(selectedValue)}`);

        if (!response.ok) {
            throw new Error(`HTTP ${response.status}: ${response.statusText}`);
        }

        const contentDisposition = response.headers.get('Content-Disposition');
        let filename = selectedValue;
        if (contentDisposition) {
            const match = contentDisposition.match(/filename="?([^"]+)"?/);
            if (match) filename = match[1];
        }

        const blob = await response.blob();
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        document.body.removeChild(a);

        console.log(`Downloaded: ${filename}`);
    } catch (error) {
        alert(`❌ Erro ao baixar CSV: ${error.message}`);
        console.error('Download error:', error);
    }
});

// Floating Logs Window Controls
const logsWindow = document.getElementById('floating-logs-window');
const logsHeader = document.getElementById('logs-window-header');
let isDragging = false;
let currentX, currentY, initialX, initialY;

// Open logs window
document.getElementById('btn-open-logs')?.addEventListener('click', () => {
    logsWindow.style.display = 'flex';
});

// Close logs window
document.getElementById('btn-close-logs')?.addEventListener('click', () => {
    logsWindow.style.display = 'none';
});

// Make window draggable
logsHeader?.addEventListener('mousedown', (e) => {
    if (e.target.closest('.floating-window-controls')) return;

    isDragging = true;
    initialX = e.clientX - logsWindow.offsetLeft;
    initialY = e.clientY - logsWindow.offsetTop;
    logsWindow.style.transform = 'none';
});

document.addEventListener('mousemove', (e) => {
    if (!isDragging) return;

    e.preventDefault();
    currentX = e.clientX - initialX;
    currentY = e.clientY - initialY;

    logsWindow.style.left = `${currentX}px`;
    logsWindow.style.top = `${currentY}px`;
});

document.addEventListener('mouseup', () => {
    isDragging = false;
});

// Clear logs button (both floating and inline)
document.getElementById('btn-clear-logs-float')?.addEventListener('click', () => {
    document.getElementById('logs-container').innerHTML = '';
});

// Helper function to escape HTML and prevent XSS
function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// Fetch and display logs from ESP32
async function updateLogs() {
    try {
        const response = await fetch('/api/logs');
        const logs = await response.json();

        const logsContainer = document.getElementById('logs-container');
        if (!logsContainer) return;

        logsContainer.innerHTML = '';

        logs.forEach(log => {
            const logEntry = document.createElement('div');
            logEntry.className = 'log-entry';

            let levelClass = '';
            let levelLabel = '';
            switch (log.level) {
                case 'error':
                    levelClass = 'log-error';
                    levelLabel = '[ERROR]';
                    break;
                case 'warn':
                    levelClass = 'log-warn';
                    levelLabel = '[WARN]';
                    break;
                case 'info':
                    levelClass = 'log-info';
                    levelLabel = '[INFO]';
                    break;
                case 'debug':
                    levelClass = 'log-debug';
                    levelLabel = '[DEBUG]';
                    break;
            }

            logEntry.classList.add(levelClass);

            const time = new Date(log.timestamp).toLocaleTimeString('pt-BR');

            const timeSpan = document.createElement('span');
            timeSpan.className = 'log-time';
            timeSpan.textContent = time;

            const levelSpan = document.createElement('span');
            levelSpan.className = 'log-level';
            levelSpan.textContent = levelLabel;

            const messageSpan = document.createElement('span');
            messageSpan.className = 'log-message';
            messageSpan.textContent = log.message;

            logEntry.appendChild(timeSpan);
            logEntry.appendChild(levelSpan);
            logEntry.appendChild(messageSpan);

            logsContainer.appendChild(logEntry);
        });
    } catch (error) {
        console.error('Error fetching logs:', error);
    }
}

// Clear logs button handler
document.getElementById('btn-clear-logs')?.addEventListener('click', async () => {
    try {
        await fetch('/api/logs/clear', { method: 'POST' });
        updateLogs(); // Refresh display
    } catch (error) {
        console.error('Error clearing logs:', error);
    }
});

// Start real-time monitoring
updateSystemInfo();
setInterval(updateSystemInfo, 1000);

loadMeasurementList();
setInterval(loadMeasurementList, 10000);

updateLogs();
setInterval(updateLogs, 2000);

// =============================================================================
// Scroll-to-change functionality for select elements
// Allows changing select options by scrolling with mouse wheel while hovering
// =============================================================================
function enableScrollOnSelect(selectElement) {
    if (!selectElement) return;

    selectElement.addEventListener('wheel', (e) => {
        // Only act if element is not disabled
        if (selectElement.disabled) return;

        // Prevent page scroll
        e.preventDefault();

        const options = selectElement.options;
        const currentIndex = selectElement.selectedIndex;

        // Determine scroll direction
        const direction = e.deltaY > 0 ? 1 : -1;

        // Calculate new index with bounds checking
        let newIndex = currentIndex + direction;

        // Skip empty/placeholder options (value === "")
        while (newIndex >= 0 && newIndex < options.length && options[newIndex].value === "") {
            newIndex += direction;
        }

        // Clamp to valid range
        if (newIndex < 0) newIndex = 0;
        if (newIndex >= options.length) newIndex = options.length - 1;

        // Skip if landing on placeholder
        if (options[newIndex].value === "" && currentIndex !== newIndex) {
            return;
        }

        // Only update if changed
        if (newIndex !== currentIndex && options[newIndex].value !== "") {
            selectElement.selectedIndex = newIndex;

            // Dispatch change event to trigger any listeners
            selectElement.dispatchEvent(new Event('change', { bubbles: true }));
        }
    }, { passive: false });

    // Visual feedback on hover
    selectElement.addEventListener('mouseenter', () => {
        selectElement.style.cursor = 'ns-resize';
    });

    selectElement.addEventListener('mouseleave', () => {
        selectElement.style.cursor = '';
    });
}

// Apply to all select-field elements
document.querySelectorAll('.select-field').forEach(select => {
    enableScrollOnSelect(select);
});

// Also apply to dynamically created selects (MutationObserver)
const selectObserver = new MutationObserver((mutations) => {
    mutations.forEach((mutation) => {
        mutation.addedNodes.forEach((node) => {
            if (node.nodeType === 1) { // Element node
                if (node.classList?.contains('select-field')) {
                    enableScrollOnSelect(node);
                }
                // Check children
                node.querySelectorAll?.('.select-field').forEach(select => {
                    enableScrollOnSelect(select);
                });
            }
        });
    });
});

selectObserver.observe(document.body, { childList: true, subtree: true });

console.log('MOSFET Characterization Dashboard initialized (Multi-Page Architecture)');

// Feature: Real-time Filename Validation
document.getElementById('filename')?.addEventListener('input', (e) => {
    const input = e.target;
    const infoText = input.nextElementSibling; // helper-text
    const validRegex = /^[a-zA-Z0-9_\-\.]+$/;

    if (input.value && !validRegex.test(input.value)) {
        input.style.borderColor = "#F44336"; // Red
        if (infoText) {
            infoText.style.color = "#F44336";
            infoText.textContent = "Nome inválido! Use apenas letras, números, _ . ou -";
        }
    } else {
        input.style.borderColor = ""; // Reset
        if (infoText) {
            infoText.style.color = "";
            infoText.textContent = "Arquivo com timestamps, VDS, VGS, Vsh, Ids, Gm, parâmetros"; // Restore original text or keep empty if dynamic
        }
    }
});

// Feature: Delete Single Measurement Logic from Visualization Tab
document.getElementById('btn-delete-measurement')?.addEventListener('click', async () => {
    const selectedValue = document.getElementById('file-select').value;
    if (!selectedValue) return;

    if (!confirm(`Tem certeza que deseja deletar o arquivo "${selectedValue}"?\nEsta ação é irreversível.`)) {
        return;
    }

    try {
        const response = await fetch(`/api/files/delete?file=${encodeURIComponent(selectedValue)}`, { method: 'POST' });
        if (!response.ok) throw new Error("Falha ao deletar arquivo");

        showToast(`Arquivo "${selectedValue}" deletado com sucesso.`, 'success');

        // Reset plots
        currentCSVData = [];
        const plotDiv = document.getElementById('plot-container');
        if (plotDiv) Plotly.purge(plotDiv);

        // Reset selection UI
        const downloadBtn = document.getElementById('btn-download-measurement');
        const deleteBtn = document.getElementById('btn-delete-measurement');
        const vdsSelect = document.getElementById('vds-select');
        const fileSelect = document.getElementById('file-select');

        if (downloadBtn) downloadBtn.disabled = true;
        if (deleteBtn) deleteBtn.disabled = true;
        if (vdsSelect) {
            vdsSelect.innerHTML = '<option value="">Aguardando arquivo...</option>';
            vdsSelect.disabled = true;
        }
        if (fileSelect) fileSelect.value = "";

        // Clear global selection
        currentSelectedFile = "";

        // Reload File List
        loadMeasurementList();

    } catch (error) {
        logErrorAndAlert("Erro ao deletar arquivo", error);
    }
});

// Feature: Delete All Logic from Main Page
document.getElementById('btn-delete-all')?.addEventListener('click', async () => {
    // 1st Confirmation
    if (!confirm("⚠️ ATENÇÃO ⚠️\n\nTem certeza que deseja DELETAR TODOS os arquivos de medição?\n\nEsta ação apagará todo o histórico de medidas salvas no ESP32.")) {
        return;
    }

    // 2nd Confirmation (Irreversible)
    if (!confirm("⛔ CONFIRMAÇÃO FINAL ⛔\n\nEsta ação é IRREVERSÍVEL.\nTodos os dados serão perdidos permanentemente.\n\nDeseja realmente continuar e apagar TUDO?")) {
        return;
    }

    try {
        const response = await fetch('/api/files/delete-all', { method: 'POST' });
        if (!response.ok) throw new Error("Falha ao deletar todos os arquivos");

        showToast("Todos os arquivos foram deletados com sucesso.", 'success');

        // Reset UI
        currentCSVData = [];
        loadMeasurementList();

    } catch (error) {
        logErrorAndAlert("Erro ao limpar armazenamento", error);
    }
});
console.log('Scroll-to-change enabled on select elements');

