/**
 * core.js - Global utilities, state management, and shared UI helpers
 * Part of ESP32 MOSFET Analysis Tool
 */

// =============================================================================
// Global Variables & State
// =============================================================================
let currentVDD = 5.0; // Default VDD voltage
let usbConnected = false;

// Debug Configuration
const DEBUG_FLAGS = {
    ENABLED: true,       // Master switch
    CSV: true,           // CSV parsing details
    PLOT: true,          // Plotting data and traces
    API: true,           // API calls and responses
    UI: true,            // UI events (clicks, toggles)
    MATH: true           // Math calculations (Gm, SS)
};

// Debug helper
function dbg(flag, ...args) {
    if (DEBUG_FLAGS.ENABLED && DEBUG_FLAGS[flag]) {
        console.log(`[DBG:${flag}]`, ...args);
    }
}

// =============================================================================
// System Info & Monitoring
// =============================================================================

// Fetch system info and update display
async function updateSystemInfo() {
    try {
        dbg('API', 'Fetching /api/system_info');
        const response = await fetch('/api/system_info');
        const data = await response.json();

        // Update temperature
        const tempEl = document.getElementById('temperature');
        if (tempEl) tempEl.textContent = `${data.temperature.toFixed(1)}°C`;

        // Update USB status
        usbConnected = data.usb_connected;
        const connStatusEl = document.getElementById('connection-status');
        if (connStatusEl) {
            connStatusEl.textContent = data.usb_connected ? 'Serial USB Ativa ✓' : 'Inativa';
            connStatusEl.style.color = data.usb_connected ? '#4CAF50' : '#F44336';
        }

        // Update header status indicator
        const headerStatusText = document.getElementById('header-status-text');
        const headerStatusDot = document.getElementById('header-status-dot');
        if (headerStatusText && headerStatusDot) {
            headerStatusText.textContent = data.usb_connected ? 'Comunicação USB Ativa' : 'Apenas WiFi';
            headerStatusDot.style.background = data.usb_connected ? '#4CAF50' : '#FFA726';
        }

        // Update chip ID
        const sensorIdEl = document.getElementById('sensor-id');
        if (sensorIdEl) sensorIdEl.textContent = data.chip_id;

        // Update Version (Dynamically add if missing)
        let versionEl = document.getElementById('fw-version');
        if (!versionEl && data.version && sensorIdEl) {
            const container = sensorIdEl.parentElement.parentElement;
            const row = document.createElement('div');
            row.className = 'info-row';
            row.innerHTML = `<span class="info-label">Versão FW</span><span class="info-value" id="fw-version" style="font-family: monospace;">${data.version}</span>`;
            if (container) container.appendChild(row);
        } else if (versionEl && data.version) {
            versionEl.textContent = data.version;
        }

        // Update free heap
        const freeHeapEl = document.getElementById('free-heap');
        if (freeHeapEl) {
            const heapKB = (data.free_heap / 1024).toFixed(1);
            freeHeapEl.textContent = `${heapKB} KB`;
        }

        // Update Debug Mode status
        let debugEl = document.getElementById('debug-status');
        if (!debugEl && freeHeapEl) {
            // Create debug status row dynamically if not exists
            const container = freeHeapEl.parentElement.parentElement;
            const row = document.createElement('div');
            row.className = 'info-row';
            row.innerHTML = `<span class="info-label">Debug Log <small style="color:#888">(GPIO12→GND)</small></span><span class="info-value" id="debug-status"></span>`;
            if (container) container.appendChild(row);
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
        // console.error('Error fetching system info:', error); // Suppress frequent errors
    }
}

// Start monitoring
document.addEventListener('DOMContentLoaded', () => {
    updateSystemInfo();
    setInterval(updateSystemInfo, 1000);
});

// =============================================================================
// UI Helpers (Toasts, Validation, Formatting)
// =============================================================================

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

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
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
document.addEventListener('DOMContentLoaded', () => {
    ['vds-start', 'vds-end', 'vgs-start', 'vgs-end'].forEach(id => {
        const input = document.getElementById(id);
        if (input) {
            input.addEventListener('change', () => validateVoltageLimit(input));
        }
    });
});

// =============================================================================
// Logs System
// =============================================================================

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
        // console.error('Error fetching logs:', error);
    }
}

document.addEventListener('DOMContentLoaded', () => {
    updateLogs();
    setInterval(updateLogs, 2000);

    // Logs Window Controls
    const logsWindow = document.getElementById('floating-logs-window');
    const logsHeader = document.getElementById('logs-window-header');

    if (logsWindow && logsHeader) {
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
        logsHeader.addEventListener('mousedown', (e) => {
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

        // Clear logs button (floating)
        document.getElementById('btn-clear-logs-float')?.addEventListener('click', () => {
            document.getElementById('logs-container').innerHTML = '';
        });
    }
});

// =============================================================================
// Scroll-to-change functionality
// =============================================================================
function enableScrollOnSelect(selectElement) {
    if (!selectElement) return;

    selectElement.addEventListener('wheel', (e) => {
        if (selectElement.disabled) return;
        e.preventDefault();

        const options = selectElement.options;
        const currentIndex = selectElement.selectedIndex;
        const direction = e.deltaY > 0 ? 1 : -1;
        let newIndex = currentIndex + direction;

        while (newIndex >= 0 && newIndex < options.length && options[newIndex].value === "") {
            newIndex += direction;
        }

        if (newIndex < 0) newIndex = 0;
        if (newIndex >= options.length) newIndex = options.length - 1;

        if (options[newIndex].value === "" && currentIndex !== newIndex) return;

        if (newIndex !== currentIndex && options[newIndex].value !== "") {
            selectElement.selectedIndex = newIndex;
            selectElement.dispatchEvent(new Event('change', { bubbles: true }));
        }
    }, { passive: false });

    selectElement.addEventListener('mouseenter', () => {
        selectElement.style.cursor = 'ns-resize';
    });
    selectElement.addEventListener('mouseleave', () => {
        selectElement.style.cursor = '';
    });
}

document.addEventListener('DOMContentLoaded', () => {
    document.querySelectorAll('.select-field').forEach(select => {
        enableScrollOnSelect(select);
    });

    const selectObserver = new MutationObserver((mutations) => {
        mutations.forEach((mutation) => {
            mutation.addedNodes.forEach((node) => {
                if (node.nodeType === 1) {
                    if (node.classList?.contains('select-field')) {
                        enableScrollOnSelect(node);
                    }
                    node.querySelectorAll?.('.select-field').forEach(select => {
                        enableScrollOnSelect(select);
                    });
                }
            });
        });
    });
    selectObserver.observe(document.body, { childList: true, subtree: true });
});
