/**
 * visualization.js - Plotting, CSV parsing, and mathematical analysis
 * Part of ESP32 MOSFET Analysis Tool
 * Dependencies: core.js, Plotly.js
 */

// =============================================================================
// Visualization State
// =============================================================================
let currentCSVData = []; // Store full parsed CSV data
let uniqueVDSValues = []; // Unique values for the curve selector (VDS in VGS mode, VGS in VDS mode)
let currentSweepMode = 'VGS'; // Sweep mode: 'VGS' (default) or 'VDS'
let fileAnalysisMap = {}; // Global map for metadata (Vt, SS, Tangents)
let scaleType = 'linear'; // 'linear' or 'log' for Ids axis

// Visible curves state
let visibleCurves = {
    ids: true,
    gm: false,
    ss: false,
    vt: false
};

// UI Elements (assigned in init)
let vizFileSelect = null;

// =============================================================================
// Initialization & Event Listeners
// =============================================================================

function initVisualization() {
    vizFileSelect = document.getElementById('file-select');

    // File Selection Logic (Visualization Tab)
    if (vizFileSelect) {
        vizFileSelect.addEventListener('change', handleFileSelection);
    }

    // Toggle Buttons (Curve Visibility)
    document.querySelectorAll('.toggle-btn[data-curve]').forEach(btn => {
        btn.addEventListener('click', () => {
            const curveType = btn.dataset.curve;
            btn.classList.toggle('active');
            const isActive = btn.classList.contains('active');

            visibleCurves[curveType] = isActive;

            // Auto-switch scale based on SS curve (User Request)
            if (curveType === 'ss') {
                dbg('UI', `SS toggled: ${isActive ? 'ON → Log Scale' : 'OFF → Linear Scale'}`);
                setScale(isActive ? 'log' : 'linear');
            }

            updatePlotsMultiCurve();
        });
    });

    // Metric Selector Injection
    initMetricSelector();

    // Download & Delete Buttons (Contextual)
    document.getElementById('btn-download-measurement')?.addEventListener('click', handleDownload);
    document.getElementById('btn-delete-measurement')?.addEventListener('click', handleDelete);

    // VDS/Curve Selector Change
    document.getElementById('vds-select')?.addEventListener('change', updatePlotsMultiCurve);

    // Scale Toggle - Passive indicator only (controlled by SS button)
    const scaleToggle = document.getElementById('scale-toggle');
    if (scaleToggle) {
        scaleToggle.disabled = true; // Disable direct interaction
        scaleToggle.parentElement.style.pointerEvents = 'none'; // Make entire toggle non-clickable
        scaleToggle.parentElement.style.opacity = '0.9'; // Slightly dimmed to indicate passive
    }
}

document.addEventListener('DOMContentLoaded', initVisualization);

// =============================================================================
// File Handling
// =============================================================================

let currentSelectedFile = '';

async function handleFileSelection(e) {
    const selectedFile = e.target.value;
    currentSelectedFile = selectedFile; // Save selection

    const downloadBtn = document.getElementById('btn-download-measurement');
    const deleteBtn = document.getElementById('btn-delete-measurement');
    const vdsSelect = document.getElementById('vds-select');

    if (!selectedFile) {
        if (downloadBtn) downloadBtn.disabled = true;
        if (deleteBtn) deleteBtn.disabled = true;
        if (vdsSelect) {
            vdsSelect.disabled = true;
            vdsSelect.innerHTML = '<option value="">Selecione um arquivo primeiro...</option>';
        }
        currentCSVData = [];
        resetChart();
        return;
    }

    if (downloadBtn) downloadBtn.disabled = true;  // Disable during load
    if (deleteBtn) deleteBtn.disabled = true;      // Disable during load

    dbg('UI', `Iniciando carregamento: ${selectedFile}`);

    if (vdsSelect) {
        vdsSelect.disabled = true;
        vdsSelect.innerHTML = '<option value="">⏳ Carregando dados...</option>';
        vdsSelect.classList.add('select-loading'); // Red pulsing border for loading state
    }

    document.body.style.cursor = 'wait';

    try {
        const response = await fetch(`/api/files/download?file=${encodeURIComponent(selectedFile)}&t=${Date.now()}`);
        if (!response.ok) throw new Error('Falha ao baixar arquivo');

        const csvText = await response.text();
        dbg('API', `File content received (${csvText.length} bytes)`);

        parseCSV(csvText);

        // Apply UI constraints based on detected sweep mode
        applyModeToUI(currentSweepMode);

        // Update Labels based on Sweep Mode
        const curveLabel = document.getElementById('curve-select-label');
        if (curveLabel) {
            curveLabel.textContent = (currentSweepMode === 'VDS') ? 'Selecionar Curva VGS:' : 'Selecionar Curva VDS:';
        }

        // Populate Curve Selector
        if (vdsSelect) {
            vdsSelect.classList.remove('select-loading'); // Remove loading state
            vdsSelect.innerHTML = '';
            uniqueVDSValues.forEach(val => {
                const option = document.createElement('option');
                option.value = val;
                const label = (currentSweepMode === 'VDS') ? `VGS = ${val.toFixed(3)} V` : `VDS = ${val.toFixed(3)} V`;
                option.textContent = label;
                vdsSelect.appendChild(option);
            });

            if (uniqueVDSValues.length > 0) vdsSelect.value = uniqueVDSValues[0];
            vdsSelect.disabled = false;
        }

        // Enable buttons as soon as data is loaded — do not depend on updatePlotsMultiCurve()
        // (that function has early returns that would leave buttons disabled)
        if (downloadBtn) downloadBtn.disabled = false;
        if (deleteBtn) deleteBtn.disabled = false;

        updatePlotsMultiCurve();

    } catch (error) {
        console.error("Error loading CSV:", error);
        showToast("Erro ao carregar arquivo de dados.", "error");
        if (vdsSelect) {
            vdsSelect.classList.remove('select-loading');
            vdsSelect.innerHTML = '<option value="">Erro ao carregar</option>';
        }
        // Re-enable buttons even on error, so user can retry or download
        if (downloadBtn) downloadBtn.disabled = false;
        if (deleteBtn) deleteBtn.disabled = false;
    } finally {
        document.body.style.cursor = 'default';
    }
}

async function handleDownload() {
    const selectedValue = document.getElementById('file-select').value;
    if (!selectedValue) {
        alert('⚠️ Selecione uma medida primeiro');
        return;
    }

    try {
        const response = await fetch(`/api/files/download?file=${encodeURIComponent(selectedValue)}`);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const blob = await response.blob();
        const url = window.URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = selectedValue; // Simplistic filename usage
        document.body.appendChild(a);
        a.click();
        window.URL.revokeObjectURL(url);
        document.body.removeChild(a);
    } catch (error) {
        alert(`❌ Erro ao baixar CSV: ${error.message}`);
    }
}

async function handleDelete() {
    const selectedValue = document.getElementById('file-select').value;
    if (!selectedValue) return;

    if (!confirm(`Tem certeza que deseja deletar "${selectedValue}"?\nEsta ação é irreversível.`)) return;

    try {
        const response = await fetch(`/api/files/delete?file=${encodeURIComponent(selectedValue)}`, { method: 'POST' });
        if (!response.ok) throw new Error("Falha ao deletar arquivo");

        showToast(`Arquivo deleto com sucesso.`, 'success');

        // Reset UI
        currentCSVData = [];
        resetChart();
        document.getElementById('file-select').value = "";
        currentSelectedFile = "";

        // Refresh List (defined in collection.js, accessible because it's global scope)
        if (typeof loadMeasurementList === 'function') loadMeasurementList();

    } catch (error) {
        showToast("Erro ao deletar arquivo: " + error.message, 'error');
    }
}

// =============================================================================
// CSV Parsing & Data Processing
// =============================================================================

function parseCSV(csvText) {
    dbg('CSV', `Parsing CSV data. Size: ${csvText.length} bytes`);
    if (csvText.length < 10) {
        showToast("Erro: Arquivo vazio ou inválido recebido.", "error");
        return;
    }

    currentCSVData = [];
    uniqueVDSValues = [];
    const lines = csvText.trim().split('\n');
    let dataStartIndex = -1;
    let rShunt = 100.0; // Default fallback
    const analysisMap = {};

    let colMap = {
        timestamp: 0, vd: 1, vg: 2, vds_sent: 1, vgs_sent: 2, vds: 1, vgs: 2,
        vd_read: 3, vg_read: 4, vsh: 5, vsh_precise: 6, vds_true: 7, vgs_true: 8, ids: 9
    };

    // 1. Header & Metadata Scan
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].trim();

        // Detect data header row and build column map
        if (line.includes('timestamp,') || line.includes('time,vds')) {
            dataStartIndex = i + 1;
            const headers = line.split(',');
            headers.forEach((h, idx) => {
                const clean = h.trim().toLowerCase();
                colMap[clean] = idx;
            });
            dbg('CSV', `Detected headers: ${headers.join('|')}`);
        }

        if (line.startsWith('# Sweep Mode:')) {
            const modeMatch = line.match(/# Sweep Mode:\s*(VGS|VDS)/i);
            if (modeMatch) currentSweepMode = modeMatch[1].toUpperCase();
        }

        if (line.startsWith('# RSHUNT=')) {
            const rMatch = line.match(/# RSHUNT=([\d\.]+)/);
            if (rMatch) rShunt = parseFloat(rMatch[1]);
        }
        // ... rest of metadata scan ...
        if (line.startsWith('# VDS=')) {
            try {
                const vdsMatch = line.match(/VDS=([\d\.]+)V/);
                const vtMatch = line.match(/Vt=([\d\.]+)V/);
                const ssMatch = line.match(/SS=([0-9\.]+)\s?mV\/dec/);
                const gmMatch = line.match(/MaxGm=([0-9\.eE\-\+]+)\s?S/);
                
                if (vdsMatch) {
                    const vdsVal = parseFloat(vdsMatch[1]);
                    const vdsKey = Math.round(vdsVal * 1000) / 1000;
                    analysisMap[vdsKey] = {
                        vt: vtMatch ? parseFloat(vtMatch[1]) : 0,
                        ss: ssMatch ? parseFloat(ssMatch[1]) : 0,
                        max_gm: gmMatch ? parseFloat(gmMatch[1]) : 0
                    };
                }
            } catch(e) {}
        }
    }

    // 2. Data Parsing
    currentCSVData = [];
    const vdsSet = new Set();
    const vgsSet = new Set();

    for (let i = dataStartIndex; i < lines.length; i++) {
        const line = lines[i].trim();
        if (!line || line.startsWith('#')) continue;
        const parts = line.split(',');
        if (parts.length < 5) continue;

        let vds, vgs, vsh, ids, gm=0, vd_read, vg_read, vds_true, vgs_true;

        // Dynamic mapping based on detected indices
        vds = parseFloat(parts[colMap.vds || colMap.vd || colMap.vds_sent || 1]);
        vgs = parseFloat(parts[colMap.vgs || colMap.vg || colMap.vgs_sent || 2]);
        vd_read = parseFloat(parts[colMap.vd_read || 3]);
        vg_read = parseFloat(parts[colMap.vg_read || 4]);
        vsh = parseFloat(parts[colMap.vsh || 5]);
        vds_true = parseFloat(parts[colMap.vds_true || 6]);
        vgs_true = parseFloat(parts[colMap.vgs_true || 7]);
        ids = parseFloat(parts[colMap.ids || 8]);

        // Specific override if vds_true/vgs_true are missing (7-col legacy)
        if (isNaN(vds_true)) vds_true = vds;
        if (isNaN(vgs_true)) vgs_true = vgs;

        // Legacy format fallback for Vt/SS (no longer stored per-row in new format)
        let vt = 0;
        let ss = 0;

        if (!isNaN(vds_true) && !isNaN(vgs_true)) {
            const vdsTrueRounded = Math.round(vds_true * 1000) / 1000;
            const vgsTrueRounded = Math.round(vgs_true * 1000) / 1000;
            const vdsRounded = Math.round(vds * 1000) / 1000;
            const vgsRounded = Math.round(vgs * 1000) / 1000;

            // Curve selector is built from COMMANDED values (the loop targets the user set)
            vdsSet.add(vdsRounded);
            vgsSet.add(vgsRounded);

            // Look up backend-computed metadata by the commanded VDS key
            if (analysisMap[vdsRounded]) {
                vt = analysisMap[vdsRounded].vt;
                ss = analysisMap[vdsRounded].ss;
            }

            currentCSVData.push({
                vds: vdsRounded,       // commanded target
                vgs: vgsRounded,       // commanded target
                vsh: isNaN(vsh) ? 0 : vsh,
                ids: isNaN(ids) ? 0 : ids,
                gm: isNaN(gm) ? 0 : gm,
                vd_read: isNaN(vd_read) ? vds : vd_read,
                vg_read: isNaN(vg_read) ? vgs : vg_read,
                vds_true: vdsTrueRounded,  // true terminal VDS (for plot x-axis)
                vgs_true: vgsTrueRounded,  // true terminal VGS (for plot x-axis)
                r_shunt: rShunt,           // Store for later trace calculation
                vt: vt,
                ss: ss,
                max_gm: analysisMap[vdsRounded] ? analysisMap[vdsRounded].max_gm : 0
            });
        }
    }

    // Curve selector lists unique COMMANDED voltage values (the loop targets):
    //   VDS sweep → unique vgs values (one curve per fixed commanded VGS)
    //   VGS sweep → unique vds values (one curve per fixed commanded VDS)
    if (currentSweepMode === 'VDS') {
        uniqueVDSValues = Array.from(vgsSet).sort((a, b) => a - b);
    } else {
        uniqueVDSValues = Array.from(vdsSet).sort((a, b) => a - b);
    }

    // Post-processing: Gm recalculation only relevant for VGS sweep mode
    if (currentSweepMode !== 'VDS') {
        calculateGmForData(currentCSVData, currentSweepMode);
    }
    calculateSSForData(currentCSVData);

    fileAnalysisMap = analysisMap;
    dbg('CSV', `Parsed ${currentCSVData.length} valid points. Mode: ${currentSweepMode}. Curves: ${uniqueVDSValues.length}`);
}

// =============================================================================
// Mode-Aware UI State
// =============================================================================

/**
 * Enable or disable the Gm / SS / Vt toggle buttons depending on sweep mode.
 * In VDS sweep mode (IdVd), those analyses don't apply to output curves.
 */
function applyModeToUI(sweepMode) {
    const isVDS = sweepMode === 'VDS';

    // Curves that only make sense in VGS sweep (transfer curve)
    const analyticalCurves = ['gm', 'ss', 'vt'];

    analyticalCurves.forEach(curveType => {
        const btn = document.getElementById(`toggle-${curveType}`);
        if (!btn) return;

        if (isVDS) {
            // Disable and visually dim
            btn.disabled = true;
            btn.style.opacity = '0.35';
            btn.style.cursor = 'not-allowed';
            btn.style.pointerEvents = 'none';
            // Deactivate if it was on
            btn.classList.remove('active');
            visibleCurves[curveType] = false;
        } else {
            // Restore to interactive
            btn.disabled = false;
            btn.style.opacity = '';
            btn.style.cursor = '';
            btn.style.pointerEvents = '';
        }
    });

    // Scale controls: only relevant in VGS mode (log scale for SS)
    const scaleSection = document.querySelector('.scale-toggle-section');
    if (scaleSection) {
        scaleSection.style.opacity = isVDS ? '0.35' : '';
        scaleSection.style.pointerEvents = isVDS ? 'none' : '';
    }

    dbg('UI', `applyModeToUI: sweepMode=${sweepMode}, analytical buttons ${isVDS ? 'disabled' : 'enabled'}`);
}

// =============================================================================
// Math Engine (Frontend Re-implementations needed for visualization)
// =============================================================================

function calculateGmForData(data, sweepMode) {
    if (!data || data.length < 2) return;
    const curves = {};
    data.forEach(d => {
        // Group by the FIXED axis true-voltage
        const key = sweepMode === 'VDS' ? d.vgs_true : d.vds_true;
        if (!curves[key]) curves[key] = [];
        curves[key].push(d);
    });

    Object.keys(curves).forEach(k => {
        const curve = curves[k];
        curve.sort((a, b) => (sweepMode === 'VDS' ? a.vds_true - b.vds_true : a.vgs_true - b.vgs_true));

        for (let i = 1; i < curve.length - 1; i++) {
            const prev = curve[i - 1];
            const next = curve[i + 1];
            // Use true voltages for dx
            const dx = sweepMode === 'VDS'
                ? (next.vds_true - prev.vds_true)
                : (next.vgs_true - prev.vgs_true);
            const dy = next.ids - prev.ids;

            if (Math.abs(dx) > 1e-6) curve[i].gm = dy / dx;
        }
    });
}

function calculateSSForData(data) {
    // Only calculate SS if in VGS sweep mode
    // Basic implementation for frontend consistency
    // Complex implementation typically in backend now
}

// =============================================================================
// Plotting
// =============================================================================

function setScale(scale) {
    if (scale === scaleType) return;
    scaleType = scale;
    updateScaleStatus();
    updatePlotsMultiCurve();
}

function updateScaleStatus() {
    // Update toggle slider position
    const scaleToggle = document.getElementById('scale-toggle');
    if (scaleToggle) {
        scaleToggle.checked = (scaleType === 'log');
    }

    // Update labels
    const linearLabel = document.getElementById('scale-linear-label');
    const logLabel = document.getElementById('scale-log-label');

    if (linearLabel && logLabel) {
        if (scaleType === 'log') {
            linearLabel.classList.remove('mode-active');
            linearLabel.classList.add('mode-dimmed');
            logLabel.classList.remove('mode-dimmed');
            logLabel.classList.add('mode-active');
        } else {
            linearLabel.classList.add('mode-active');
            linearLabel.classList.remove('mode-dimmed');
            logLabel.classList.add('mode-dimmed');
            logLabel.classList.remove('mode-active');
        }
    }
}

function updatePlotsMultiCurve() {
    const vdsSelect = document.getElementById('vds-select');
    if (!vdsSelect || currentCSVData.length === 0) return;

    dbg('PLOT', `updatePlotsMultiCurve — mode: ${currentSweepMode}`);

    const isVDSMode = currentSweepMode === 'VDS';

    // Colors
    const colors = {
        ids: '#2196F3', gm: '#FF9800', ss: '#F44336', vt: '#4CAF50', tangent: '#E91E63'
    };

    // ── Filter Data ─────────────────────────────────────────────────────────
    // curveVal comes from the drop-down which lists COMMANDED voltages.
    // We filter by the commanded target so the user sees exactly the curves
    // they configured (e.g. 3.0, 3.1, 3.2, 3.3 V).
    // X-axis and calculations still use vds_true / vgs_true.
    const curveVal = parseFloat(vdsSelect.value);
    if (isNaN(curveVal)) return;

    let plotData;
    if (isVDSMode) {
        // IdVd: fixed commanded VGS; X axis = VDS_true
        plotData = currentCSVData.filter(d => Math.abs(d.vgs - curveVal) < 0.0015);
        plotData.sort((a, b) => a.vds_true - b.vds_true);
    } else {
        // IdVg: fixed commanded VDS; X axis = VGS_true
        plotData = currentCSVData.filter(d => Math.abs(d.vds - curveVal) < 0.0015);
        plotData.sort((a, b) => a.vgs_true - b.vgs_true);
    }

    if (plotData.length === 0) {
        dbg('PLOT', `No data for curveVal=${curveVal} (mode ${currentSweepMode})`);
        return;
    }

    const xData = plotData.map(d => isVDSMode ? d.vds_true : d.vgs_true);

    // ── Traces ──────────────────────────────────────────────────────────────
    // 1. Ids trace (always present)
    const traces = [{
        x: xData,
        y: plotData.map(d => Math.abs(d.ids)),
        mode: 'lines',
        name: 'Ids (A) [Lupa/Precise]',
        line: { color: colors.ids, width: 2 }
    }, {
        x: xData,
        y: plotData.map(d => Math.abs(d.vsh / (d.r_shunt || 100.0))), 
        mode: 'lines',
        name: 'Ids (A) [Bruto/LowRes]',
        visible: 'legendonly', // Hidden by default
        line: { color: '#9e9e9e', width: 1, dash: 'dash' }
    }];

    // 2. Gm trace — only in VGS mode (transfer curves)
    if (!isVDSMode && visibleCurves.gm) {
        traces.push({
            x: xData,
            y: plotData.map(d => d.gm || 0),
            mode: 'lines',
            name: 'Gm (S)',
            yaxis: 'y2',
            line: { color: colors.gm, width: 1.5, dash: 'dot' }
        });
    }

    // ── Shapes / Annotations (VGS mode only) ────────────────────────────────
    const shapes = [];
    const annotations = [];

    if (!isVDSMode) {
        const vdsKey = Math.round(curveVal * 1000) / 1000;
        const meta = fileAnalysisMap[vdsKey];

        if (meta) {
            // Vt vertical line
            if (meta.vt > 0 && visibleCurves.vt) {
                shapes.push({
                    type: 'line',
                    x0: meta.vt, y0: 0, x1: meta.vt, y1: 1,
                    xref: 'x', yref: 'paper',
                    line: { color: colors.vt, width: 2, dash: 'dash' }
                });
                annotations.push({
                    x: meta.vt, y: 1, xref: 'x', yref: 'paper',
                    text: `Vt=${meta.vt.toFixed(2)}V`,
                    showarrow: false, yanchor: 'bottom', font: { color: colors.vt }
                });
            }

            // SS tangent (log scale only)
            if (scaleType === 'log' && meta.ssTangent && visibleCurves.ss) {
                traces.push({
                    x: [meta.ssTangent.x1, meta.ssTangent.x2],
                    y: [Math.pow(10, meta.ssTangent.y1), Math.pow(10, meta.ssTangent.y2)],
                    mode: 'lines',
                    name: `SS (${meta.ss.toFixed(0)} mV/dec)`,
                    line: { color: colors.ss, width: 2 }
                });
            }
        }
    }

    // ── Layout ───────────────────────────────────────────────────────────────
    const plotTitle = isVDSMode
        ? `Curva de Saída — VGS_true = ${curveVal.toFixed(3)} V`
        : `Curva de Transferência — VDS_true = ${curveVal.toFixed(3)} V`;

    const xAxisTitle = isVDSMode ? 'VDS_true (V)' : 'VGS_true (V)';

    const layout = {
        title: plotTitle,
        xaxis: { title: xAxisTitle },
        yaxis: {
            title: 'Ids (Precise) [A]',
            titlefont: { color: '#2196F3' },
            tickfont: { color: '#2196F3' },
            type: isVDSMode ? 'linear' : scaleType,  // IdVd always linear
            exponentformat: 'e'
        },
        yaxis2: {
            title: 'Transcondutância (S)',
            titlefont: { color: '#FF9800' },
            tickfont: { color: '#FF9800' },
            overlaying: 'y',
            side: 'right',
            showgrid: false
        },
        shapes: shapes,
        annotations: annotations,
        hovermode: 'closest',
        paper_bgcolor: '#1e1e1e',
        plot_bgcolor: '#1e1e1e',
        font: { color: '#e0e0e0' }
    };

    Plotly.newPlot('plot-container', traces, layout);

    // Update metrics panel (only relevant in VGS mode)
    const vdsKey = !isVDSMode ? Math.round(curveVal * 1000) / 1000 : null;
    const meta = vdsKey !== null ? fileAnalysisMap[vdsKey] : null;
    updateMetrics(plotData, meta);
}

function updateMetrics(plotData, meta) {
    const vtEl = document.getElementById('metric-vt');
    const gmEl = document.getElementById('metric-gm');
    const ssEl = document.getElementById('metric-ss');

    if (currentSweepMode === 'VDS') {
        const msg = "N/A (VDS Mode)";
        if (vtEl) vtEl.textContent = msg;
        if (gmEl) gmEl.textContent = msg;
        if (ssEl) ssEl.textContent = msg;
        return;
    }

    // Use metadata if available, else calc
    if (meta) {
        if (vtEl) vtEl.textContent = meta.vt ? `${meta.vt.toFixed(3)} V` : '-';
        if (gmEl) gmEl.textContent = meta.max_gm ? `${(meta.max_gm * 1000).toFixed(3)} mS` : '-';
        if (ssEl) ssEl.textContent = meta.ss ? `${meta.ss.toFixed(1)} mV/dec` : '-';
    } else {
        // Fallback calc
        let maxGm = 0;
        plotData.forEach(d => { if (d.gm > maxGm) maxGm = d.gm; });
        if (gmEl) gmEl.textContent = `${(maxGm * 1000).toFixed(3)} mS`;
    }
}

function resetChart() {
    const plotDiv = document.getElementById('plot-container');
    if (plotDiv) Plotly.purge(plotDiv);
}

function initMetricSelector() {
    const controls = document.querySelector('.viz-controls');
    if (controls && !document.getElementById('plot-type-select')) {
        const select = document.createElement('select');
        select.id = 'plot-type-select';
        select.className = 'control-input';
        select.innerHTML = `
            <option value="ids">Corrente (Ids)</option>
            <option value="gm">Transcondutância (Gm)</option>
            <option value="ss">Subthreshold Swing (SS)</option>
        `;
        // This selector concept was in dashboard.js but not fully connected to updatePlots. 
        // Adding simplistic listener for now.
        select.addEventListener('change', () => {
            // Logic to switch primary trace? Currently we show all enabled.
        });
        controls.appendChild(select);
    }
}
