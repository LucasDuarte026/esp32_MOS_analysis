// Email Page Logic (V3.1 - Dynamic Credentials)

let emailStatusInterval = null;

function initEmailPage() {
    console.log('Initializing Email Page (Dynamix)...');
    loadFileList();

    // Form Submission
    const form = document.getElementById('email-form');
    if (form) {
        form.addEventListener('submit', handleEmailSubmit);
    }

    // Select All Checkbox
    const selectAll = document.getElementById('select-all-files');
    if (selectAll) {
        selectAll.addEventListener('change', toggleSelectAll);
    }

    // Provider Config Logic
    const providerSelect = document.getElementById('smtp-provider');
    if (providerSelect) {
        providerSelect.addEventListener('change', handleProviderChange);
    }

    // Initial check (polling)
    pollEmailStatus();
}

function handleProviderChange(e) {
    const provider = e.target.value;
    const hostInput = document.getElementById('smtp-host');
    const portInput = document.getElementById('smtp-port');

    if (!hostInput || !portInput) return;

    const configs = {
        gmail: { host: "smtp.gmail.com", port: 465 },
        outlook: { host: "smtp.office365.com", port: 587 },
        yahoo: { host: "smtp.mail.yahoo.com", port: 465 },
        custom: { host: "", port: 587 }
    };

    if (configs[provider]) {
        hostInput.value = configs[provider].host;
        portInput.value = configs[provider].port;

        if (provider === 'custom') {
            hostInput.readOnly = false;
            portInput.readOnly = false;
            hostInput.focus();
        } else {
            hostInput.readOnly = true;
            portInput.readOnly = true;
        }
    }
}

async function loadFileList() {
    const container = document.getElementById('file-list-container');
    if (!container) return;

    container.innerHTML = '<p class="loading-text">Carregando arquivos...</p>';

    try {
        const response = await fetch('/api/files');
        if (!response.ok) throw new Error('Falha ao listar arquivos');

        const data = await response.json();
        renderFileList(data.files || []);
    } catch (error) {
        console.error('Error loading files:', error);
        container.innerHTML = `<p class="error-text">Erro: ${error.message}</p>`;
    }
}

function renderFileList(files) {
    const container = document.getElementById('file-list-container');
    if (!container) return;

    if (files.length === 0) {
        container.innerHTML = '<p class="empty-text">Nenhum arquivo encontrado na memória.</p>';
        return;
    }

    container.innerHTML = ''; // Clear loading

    files.forEach(file => {
        const item = document.createElement('div');
        item.className = 'file-item';

        const timestamp = new Date(file.timestamp * 1000).toLocaleString();
        const sizeKB = (file.size / 1024).toFixed(1);

        item.innerHTML = `
            <label class="file-checkbox-label">
                <input type="checkbox" name="selected_files" value="${file.name}">
                <div class="file-info">
                    <span class="file-name">${file.name}</span>
                    <span class="file-meta">${sizeKB} KB • ${timestamp}</span>
                </div>
            </label>
        `;
        container.appendChild(item);
    });
}

function toggleSelectAll(e) {
    const checkboxes = document.querySelectorAll('input[name="selected_files"]');
    checkboxes.forEach(cb => cb.checked = e.target.checked);
}

async function handleEmailSubmit(e) {
    e.preventDefault();

    // Core Fields
    const to = document.getElementById('email-recipients').value;
    const cc = document.getElementById('email-cc')?.value || "";
    const subject = document.getElementById('email-subject').value;
    const body = document.getElementById('email-message').value;

    // Credentials
    const senderEmail = document.getElementById('sender-email').value;
    const senderPass = document.getElementById('sender-password').value;
    const smtpHost = document.getElementById('smtp-host').value;
    const smtpPort = document.getElementById('smtp-port').value;

    if (!senderEmail || !senderPass || !smtpHost) {
        showToast('Credenciais de email incompletas.', 'error');
        return;
    }

    // Get selected files
    const checkboxes = document.querySelectorAll('input[name="selected_files"]:checked');
    const files = Array.from(checkboxes).map(cb => cb.value);

    if (files.length === 0) {
        if (!confirm("Nenhum arquivo selecionado. Enviar mesmo assim?")) {
            return;
        }
    }

    const payload = {
        to,
        cc,
        subject,
        body,
        files,
        sender_email: senderEmail,
        sender_password: senderPass,
        smtp_host: smtpHost,
        smtp_port: parseInt(smtpPort)
    };

    setFormBusy(true);

    try {
        const response = await fetch('/api/email/send', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });

        if (response.status === 429) {
            showToast('Sistema ocupado enviando outro email. Tente novamente em breve.', 'warning');
            setFormBusy(false);
            return;
        }

        if (!response.ok) {
            const err = await response.json();
            throw new Error(err.message || 'Erro desconhecido');
        }

        showToast('Envio iniciado! Verifique o console ou a barra de progresso.', 'success');
        startStatusPolling();

    } catch (error) {
        console.error(error);
        showToast('Erro ao iniciar envio: ' + error.message, 'error');
        setFormBusy(false);
    }
}

function startStatusPolling() {
    if (emailStatusInterval) clearInterval(emailStatusInterval);
    emailStatusInterval = setInterval(pollEmailStatus, 1000);
}

async function pollEmailStatus() {
    try {
        const response = await fetch('/api/email/status');
        if (!response.ok) return;

        const status = await response.json();
        updateProgressBar(status);

        if (status.status === 'SUCCESS' || status.status === 'FAILED') {
            clearInterval(emailStatusInterval);
            emailStatusInterval = null;
            setFormBusy(false);

            if (status.status === 'SUCCESS') {
                showToast('Email enviado com sucesso!', 'success');
            } else {
                showToast('Falha no envio: ' + status.message, 'error');
            }
        } else if (status.status !== 'IDLE') {
            if (!emailStatusInterval) startStatusPolling();
            setFormBusy(true);
        }

    } catch (e) {
        console.warn('Status poll failed', e);
    }
}

function updateProgressBar(status) {
    const progressContainer = document.getElementById('email-progress-container');
    const progressBar = document.getElementById('email-progress-bar');
    const statusText = document.getElementById('email-status-text');

    if (!progressContainer) return;

    if (status.status === 'IDLE') {
        progressContainer.style.display = 'none';
        return;
    }

    progressContainer.style.display = 'block';

    // Simulate real progress or use backend value
    let prog = status.progress;
    if (prog < 0) prog = 10; // Indeterminate state (uploading file)

    progressBar.style.width = `${prog}%`;

    let text = status.message || status.status;
    if (status.file) text += ` (${status.file})`;
    statusText.textContent = text;

    if (status.status === 'FAILED') {
        statusText.style.color = '#ff5555';
    } else if (status.status === 'SUCCESS') {
        statusText.style.color = '#50fa7b';
    } else {
        statusText.style.color = '';
    }
}

function setFormBusy(busy) {
    const btn = document.querySelector('#email-form button[type="submit"]');
    // Disable inputs to prevent changes during send
    const inputs = document.querySelectorAll('#email-form input, #email-form textarea, #email-form select');

    if (busy) {
        if (btn) {
            btn.disabled = true;
            btn.textContent = 'Enviando...';
        }
        inputs.forEach(el => el.disabled = true);
    } else {
        if (btn) {
            btn.disabled = false;
            btn.innerHTML = `
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2">
                    <path d="M22 2L11 13" />
                    <path d="M22 2L15 22L11 13L2 9L22 2Z" />
                </svg>
                Enviar Email
            `;
        }
        inputs.forEach(el => el.disabled = false);
    }
}

function showToast(msg, type = 'info') {
    if (window.showToast) {
        window.showToast(msg, type);
    } else {
        alert(`${type.toUpperCase()}: ${msg}`);
    }
}

document.addEventListener('DOMContentLoaded', initEmailPage);
