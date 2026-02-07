/**
 * collection.js - Logic for data collection, API control, and file management
 * Part of ESP32 MOSFET Analysis Tool
 */

// =============================================================================
// Measurement List Management
// =============================================================================

// Load available measurements from ESP32
async function loadMeasurementList() {
    // Note: file-select might be present on multiple pages or sections.
    // We try to find it, but don't error if missing (e.g. if running in isolation)
    const select = document.getElementById('file-select');
    if (!select) return;

    // Remember current selection to prevent annoying resets
    const previousSelection = select.value;

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

// Auto-refresh list
document.addEventListener('DOMContentLoaded', () => {
    loadMeasurementList();
    setInterval(loadMeasurementList, 10000);
});

// =============================================================================
// Collection Control (Start, Stop, Poll)
// =============================================================================

document.addEventListener('DOMContentLoaded', () => {
    const btnStart = document.getElementById('btn-start-collection');
    if (!btnStart) return; // Not on collection page

    // Start measurement button
    btnStart.addEventListener('click', async (e) => {
        console.log("Start button clicked");
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
            console.warn("Validation failed:", errorMsg);
            alert('⚠️ Erro na configuração:\n' + errorMsg);
            return;
        }

        // Get sweep mode from toggle
        const sweepModeToggle = document.getElementById('sweep-mode-toggle');
        const sweepMode = sweepModeToggle && sweepModeToggle.checked ? 'VDS' : 'VGS';

        // Get oversampling setting
        const oversamplingToggle = document.getElementById('oversampling-toggle');
        const oversamplingEnabled = oversamplingToggle ? oversamplingToggle.checked : true;

        const config = {
            vgs_start: vgsStart,
            vgs_end: vgsEnd,
            vgs_step: vgsStep,
            vds_start: vdsStart,
            vds_end: vdsEnd,
            vds_step: vdsStep,
            rshunt: rshunt,
            settling_ms: settlingTime || 5,
            oversampling: oversamplingEnabled ? 64 : 1,  // 64x or 1x (disabled)
            filename: filename || 'mosfet_data.csv',
            sweep_mode: sweepMode,
            timestamp: Math.floor(Date.now() / 1000)
        };

        try {
            console.log("Sending start request", config);
            // Disable button
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
            } else if (response.status === 507) {
                // Storage Full - show modal
                if (typeof showStorageFullModal === 'function') {
                    showStorageFullModal();
                } else {
                    alert('⚠️ Armazenamento Cheio! (Erro 507)');
                }
                resetCollectionButton();
            } else {
                const result = await response.json();
                throw new Error(result.error || 'Falha ao iniciar');
            }

        } catch (error) {
            console.error("Start error:", error);
            showToast(`❌ Falha: ${error.message}`, "error");
            resetCollectionButton();
        }
    });
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
                // Refresh list
                loadMeasurementList();
            } else {
                showToast("⚠️ Medição finalizada", "warning");
            }
            resetCollectionButton();
        }
    } catch (e) {
        // console.error("Polling error", e);
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

// =============================================================================
// Helper Controls
// =============================================================================

// Clear logs button
document.getElementById('btn-clear-logs')?.addEventListener('click', () => {
    // Clear local display
    document.getElementById('logs-container').innerHTML = '';

    // Clear backend logs
    fetch('/api/logs/clear', { method: 'POST' }).catch(console.error);
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

// Real-time Filename Validation
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
            infoText.textContent = "Arquivo com timestamps, VDS, VGS, Vsh, Ids, Gm, parâmetros";
        }
    }
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

// Oversampling Toggle - update visual state
document.getElementById('oversampling-toggle')?.addEventListener('change', (e) => {
    const offLabel = document.getElementById('oversampling-off-label');
    const onLabel = document.getElementById('oversampling-on-label');

    if (e.target.checked) {
        // Oversampling ON
        offLabel.classList.remove('mode-active');
        offLabel.classList.add('mode-dimmed');
        onLabel.classList.remove('mode-dimmed');
        onLabel.classList.add('mode-active');
    } else {
        // Oversampling OFF
        offLabel.classList.add('mode-active');
        offLabel.classList.remove('mode-dimmed');
        onLabel.classList.add('mode-dimmed');
        onLabel.classList.remove('mode-active');
    }
});

// =============================================================================
// Storage & Delete All Logic
// =============================================================================

function showStorageFullModal() {
    const modal = document.getElementById('storage-full-modal');
    if (modal) {
        modal.style.display = 'flex';
    } else {
        alert('⚠️ Armazenamento Cheio!\n\nO limite de 80% foi atingido.');
    }
}

function hideStorageFullModal() {
    const modal = document.getElementById('storage-full-modal');
    if (modal) modal.style.display = 'none';
}

document.getElementById('btn-close-storage-modal')?.addEventListener('click', hideStorageFullModal);
document.getElementById('storage-full-modal')?.addEventListener('click', (e) => {
    if (e.target.id === 'storage-full-modal') hideStorageFullModal();
});

// Delete All (Main Page)
document.getElementById('btn-delete-all')?.addEventListener('click', async () => {
    if (!confirm("⚠️ ATENÇÃO ⚠️\n\nTem certeza que deseja DELETAR TODOS os arquivos?")) return;
    if (!confirm("⛔ CONFIRMAÇÃO FINAL ⛔\n\nEsta ação é IRREVERSÍVEL.")) return;

    try {
        const response = await fetch('/api/files/delete-all', { method: 'POST' });
        if (!response.ok) throw new Error("Falha ao deletar todos os arquivos");
        showToast("Todos os arquivos foram deletados.", 'success');
        loadMeasurementList();
    } catch (error) {
        showToast("Erro ao limpar armazenamento: " + error.message, 'error');
    }
});

// Delete All (Modal)
document.getElementById('btn-modal-delete-all')?.addEventListener('click', async () => {
    if (!confirm("⚠️ ATENÇÃO ⚠️\n\nTem certeza que deseja DELETAR TODOS os arquivos?")) return;
    if (!confirm("⛔ CONFIRMAÇÃO FINAL ⛔\n\nEsta ação é IRREVERSÍVEL.")) return;

    try {
        const response = await fetch('/api/files/delete-all', { method: 'POST' });
        if (!response.ok) throw new Error("Falha ao deletar todos os arquivos");
        showToast("Todos os arquivos foram deletados.", 'success');
        hideStorageFullModal();
        loadMeasurementList();
    } catch (error) {
        showToast("Erro ao limpar armazenamento: " + error.message, 'error');
    }
});

// Close modal with Escape key
document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') hideStorageFullModal();
});
