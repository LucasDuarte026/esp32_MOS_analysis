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
    const analysisMap = {};

    // 1. Header & Metadata Scan
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].trim();

        if (line.includes('timestamp,vds') || line.includes('time,vds')) {
            dataStartIndex = i + 1;
        }

        if (line.startsWith('# Sweep Mode:')) {
            const modeMatch = line.match(/# Sweep Mode:\s*(VGS|VDS)/i);
            if (modeMatch) currentSweepMode = modeMatch[1].toUpperCase();
        }

        if (line.startsWith('# VDS=')) {
            // Parse Metadata (Vt, SS, Tangents)
            try {
                const vdsMatch = line.match(/VDS=([\d\.]+)V/);
                const vtMatch = line.match(/Vt=([\d\.]+)V/);
                const ssMatch = line.match(/SS=([0-9\.]+)\s?mV\/dec/);
                const gmMatch = line.match(/MaxGm=([0-9\.eE\-\+]+)\s?S/);
                const tanVgsMatch = line.match(/SS_Tangent_VGS:([\d\.\-]+),([\d\.\-]+)/);
                const tanLogMatch = line.match(/SS_Tangent_LogId:([\d\.\-]+),([\d\.\-]+)/);

                if (vdsMatch) {
                    const vdsVal = parseFloat(vdsMatch[1]);
                    const vdsKey = Math.round(vdsVal * 1000) / 1000;

                    const meta = {
                        vt: vtMatch ? parseFloat(vtMatch[1]) : 0,
                        ss: ssMatch ? parseFloat(ssMatch[1]) : 0,
                        max_gm: gmMatch ? parseFloat(gmMatch[1]) : 0
                    };

                    if (tanVgsMatch && tanLogMatch) {
                        meta.ssTangent = {
                            x1: parseFloat(tanVgsMatch[1]),
                            x2: parseFloat(tanVgsMatch[2]),
                            y1: parseFloat(tanLogMatch[1]),
                            y2: parseFloat(tanLogMatch[2])
                        };
                    }
                    analysisMap[vdsKey] = meta;
                }
            } catch (e) {
                console.warn("Metadata parse error:", line);
            }
        }
    }

    // Fallback if no header found
    if (dataStartIndex === -1) {
        for (let i = 0; i < lines.length; i++) {
            if (!lines[i].trim().startsWith('#') && lines[i].includes(',')) {
                dataStartIndex = i;
                break;
            }
        }
    }
    if (dataStartIndex === -1) dataStartIndex = 0;

    const vdsSet = new Set();
    const vgsSet = new Set();

    // 2. Data Parsing
    for (let i = dataStartIndex; i < lines.length; i++) {
        const parts = lines[i].split(',');
        if (parts.length < 5) continue;

        const vds = parseFloat(parts[1]);
        const vgs = parseFloat(parts[2]);
        const vsh = parseFloat(parts[3]);
        const ids = parseFloat(parts[4]);
        const gm = parseFloat(parts[5]);

        // Legacy format fallback
        let vt = (parts.length > 6) ? parseFloat(parts[6]) : 0;
        let ss = (parts.length > 7) ? parseFloat(parts[7]) : 0;

        if (!isNaN(vds) && !isNaN(vgs)) {
            const vdsRounded = Math.round(vds * 1000) / 1000;
            const vgsRounded = Math.round(vgs * 1000) / 1000;
            vdsSet.add(vdsRounded);
            vgsSet.add(vgsRounded);

            if (analysisMap[vdsRounded]) {
                vt = analysisMap[vdsRounded].vt;
                ss = analysisMap[vdsRounded].ss;
            }

            currentCSVData.push({
                vds: vdsRounded,
                vgs: vgsRounded,
                vsh: vsh,
                ids: isNaN(ids) ? 0 : ids,
                gm: isNaN(gm) ? 0 : gm,
                vt: vt,
                ss: ss,
                max_gm: analysisMap[vdsRounded] ? analysisMap[vdsRounded].max_gm : 0
            });
        }
    }

    // In VDS sweep mode (IdVd): the curve selector lists unique VGS values (one curve per VGS).
    // In VGS sweep mode (IdVg): the curve selector lists unique VDS values (one curve per VDS).
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
        const key = sweepMode === 'VDS' ? d.vgs : d.vds;
        if (!curves[key]) curves[key] = [];
        curves[key].push(d);
    });

    Object.keys(curves).forEach(k => {
        const curve = curves[k];
        curve.sort((a, b) => (sweepMode === 'VDS' ? a.vds - b.vds : a.vgs - b.vgs));

        for (let i = 1; i < curve.length - 1; i++) {
            const prev = curve[i - 1];
            const next = curve[i + 1];
            const dx = sweepMode === 'VDS' ? (next.vds - prev.vds) : (next.vgs - prev.vgs);
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
    // curveVal is the FIXED axis value used to select one curve:
    //   VDS mode → curveVal is a VGS value (fixed gate); X axis = VDS
    //   VGS mode → curveVal is a VDS value (fixed drain); X axis = VGS
    const curveVal = parseFloat(vdsSelect.value);
    if (isNaN(curveVal)) return;

    let plotData;
    if (isVDSMode) {
        // IdVd: show Ids vs VDS for the selected VGS
        plotData = currentCSVData.filter(d => Math.abs(d.vgs - curveVal) < 0.0015);
        plotData.sort((a, b) => a.vds - b.vds);
    } else {
        // IdVg: show Ids vs VGS for the selected VDS
        plotData = currentCSVData.filter(d => Math.abs(d.vds - curveVal) < 0.0015);
        plotData.sort((a, b) => a.vgs - b.vgs);
    }

    if (plotData.length === 0) {
        dbg('PLOT', `No data for curveVal=${curveVal} (mode ${currentSweepMode})`);
        return;
    }

    const xData = plotData.map(d => isVDSMode ? d.vds : d.vgs);

    // ── Traces ──────────────────────────────────────────────────────────────
    // 1. Ids trace (always present)
    const traces = [{
        x: xData,
        y: plotData.map(d => Math.abs(d.ids)),
        mode: 'lines',
        name: 'Ids (A)',
        line: { color: colors.ids, width: 2 }
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
        ? `Curva de Saída — VGS = ${curveVal.toFixed(3)} V`
        : `Curva de Transferência — VDS = ${curveVal.toFixed(3)} V`;

    const xAxisTitle = isVDSMode ? 'VDS (V)' : 'VGS (V)';

    const layout = {
        title: plotTitle,
        xaxis: { title: xAxisTitle },
        yaxis: {
            title: 'Ids (A)',
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
