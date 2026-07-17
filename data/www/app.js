document.addEventListener('DOMContentLoaded', () => {
    // Tab switching logic
    const tabs = document.querySelectorAll('.tab');
    const contents = document.querySelectorAll('.tab-content');
    const pageTitle = document.querySelector('.page-title');

    // Imposta il titolo iniziale
    const activeTab = document.querySelector('.tab.active');
    if (activeTab && pageTitle) {
        pageTitle.textContent = activeTab.textContent.trim();
    }

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const target = tab.getAttribute('data-target');
            if (target === 'ota') {
                window.location.href = '/update';
                return;
            }

            tabs.forEach(t => t.classList.remove('active'));
            contents.forEach(c => c.classList.remove('active'));
            
            tab.classList.add('active');
            document.getElementById(`content-${target}`).classList.add('active');
            
            // Aggiorna il titolo della pagina
            if (pageTitle) {
                pageTitle.textContent = tab.textContent.trim();
            }
        });
    });

    const ssidInput = document.getElementById('ssid');
    const pwdInput = document.getElementById('password');
    const btnConnect = document.getElementById('btn-connect');
    const togglePwd = document.getElementById('toggle-pwd');

    // Password visibility toggle
    togglePwd.addEventListener('click', () => {
        if(pwdInput.type === 'password') {
            pwdInput.type = 'text';
            togglePwd.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72-1.07a3 3 0 11-4.24-4.24M1 1l22 22"/></svg>`;
        } else {
            pwdInput.type = 'password';
            togglePwd.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>`;
        }
    });

    // Connect form submission
    document.getElementById('wifi-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        btnConnect.innerHTML = `<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div> Handshake...`;
        btnConnect.disabled = true;
        pwdInput.disabled = true;
        
        try {
            const response = await fetch('/api/wifi/connect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    ssid: ssidInput.value,
                    password: pwdInput.value
                })
            });
            
            if (!response.ok) throw new Error('Connect failed');
            
            btnConnect.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg> Saved & Connecting`;
            btnConnect.style.background = 'var(--success)';
            btnConnect.style.borderColor = 'var(--success)';
            btnConnect.style.color = '#000';
            btnConnect.style.boxShadow = '0 0 20px rgba(0, 255, 157, 0.4)';
            
            document.querySelector('.status-indicator').classList.remove('warning');
            document.querySelector('.status-indicator').style.background = 'var(--success)';
            document.querySelector('.status-indicator').style.boxShadow = '0 0 8px rgba(0, 255, 157, 0.5)';
            document.querySelector('.status-indicator').style.animation = 'none';
            document.querySelector('.system-status span').textContent = 'Restarting...';
            
            setTimeout(() => {
                alert('Credentials saved! Device will now restart to apply changes.');
            }, 500);
        } catch (error) {
            btnConnect.innerHTML = `<span>Error</span>`;
            btnConnect.disabled = false;
            pwdInput.disabled = false;
            alert('Failed to send configuration.');
        }
    });

    // NUT form functionality
    const nutUpsNameInput = document.getElementById('nut-upsname');
    const nutUsernameInput = document.getElementById('nut-username');
    const nutPwdInput = document.getElementById('nut-password');
    const btnSaveNut = document.getElementById('btn-save-nut');
    const toggleNutPwd = document.getElementById('toggle-nut-pwd');

    if(toggleNutPwd) {
        toggleNutPwd.addEventListener('click', () => {
            if(nutPwdInput.type === 'password') {
                nutPwdInput.type = 'text';
                toggleNutPwd.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19m-6.72-1.07a3 3 0 11-4.24-4.24M1 1l22 22"/></svg>`;
            } else {
                nutPwdInput.type = 'password';
                toggleNutPwd.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/><circle cx="12" cy="12" r="3"/></svg>`;
            }
        });
    }

    if(document.getElementById('nut-form')) {
        document.getElementById('nut-form').addEventListener('submit', async (e) => {
            e.preventDefault();
            btnSaveNut.innerHTML = `<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div> Saving...`;
            btnSaveNut.disabled = true;
            
            try {
                const response = await fetch('/api/nut/config', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        ups_name: nutUpsNameInput.value,
                        username: nutUsernameInput.value,
                        password: nutPwdInput.value
                    })
                });
                
                if (!response.ok) throw new Error('Save failed');
                
                btnSaveNut.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg> Saved!`;
                btnSaveNut.style.background = 'var(--success)';
                btnSaveNut.style.borderColor = 'var(--success)';
                btnSaveNut.style.color = '#000';
                btnSaveNut.style.boxShadow = '0 0 20px rgba(0, 255, 157, 0.4)';
                
                setTimeout(() => {
                    btnSaveNut.innerHTML = `<span class="glow"></span> Save Credentials`;
                    btnSaveNut.style = '';
                    btnSaveNut.disabled = false;
                }, 2000);
            } catch (error) {
                btnSaveNut.innerHTML = `<span>Error</span>`;
                btnSaveNut.disabled = false;
                alert('Failed to save NUT configuration.');
            }
        });
    }

    // Logs functionality
    const terminalOutput = document.getElementById('terminal-output');
    const btnClearLogs = document.getElementById('btn-clear-logs');
    const btnRefresh = document.getElementById('btn-refresh');
    const btnDownload = document.getElementById('btn-download');

    if(btnClearLogs) {
        btnClearLogs.addEventListener('click', () => {
            terminalOutput.innerHTML = '';
            addLogEntry('INFO', 'System logs cleared manually.');
        });
    }

    if(btnRefresh) {
        btnRefresh.addEventListener('click', () => {
            const btnIcon = btnRefresh.querySelector('svg');
            btnIcon.style.animation = 'spin 1s linear infinite';
            
            fetchLogs().finally(() => {
                setTimeout(() => {
                    btnIcon.style.animation = 'none';
                }, 500);
            });
        });
    }

    if(btnDownload) {
        btnDownload.addEventListener('click', () => {
            addLogEntry('INFO', 'Downloading logs...');
            window.location.href = '/api/logs';
        });
    }

    function addLogEntry(level, msg) {
        if(!terminalOutput) return;
        const now = new Date();
        const timeStr = `[${now.getHours().toString().padStart(2, '0')}:${now.getMinutes().toString().padStart(2, '0')}:${now.getSeconds().toString().padStart(2, '0')}]`;
        
        let levelClass = 'terminal-level-info';
        if(level === 'WARN') levelClass = 'terminal-level-warn';
        if(level === 'ERROR') levelClass = 'terminal-level-err';

        const line = document.createElement('div');
        line.className = 'terminal-line';
        
        // Escape HTML to prevent XSS
        const escapeHtml = (text) => {
            const div = document.createElement('div');
            div.innerText = text;
            return div.innerHTML;
        };

        line.innerHTML = `<span class="terminal-time">${timeStr}</span><span class="${levelClass}">[${level}]</span> <span class="terminal-msg">${escapeHtml(msg)}</span>`;
        
        terminalOutput.appendChild(line);
        terminalOutput.scrollTop = terminalOutput.scrollHeight;
    }

    let lastLogId = 0;

    async function fetchLogs() {
        if (!document.getElementById('content-logs').classList.contains('active')) return;
        
        try {
            const response = await fetch('/api/logs');
            if (response.ok) {
                const logs = await response.json();
                
                let maxId = lastLogId;
                for (let i = 0; i < logs.length; i++) {
                    if (logs[i].id > lastLogId) {
                        addLogEntry(logs[i].level, logs[i].msg);
                        if (logs[i].id > maxId) {
                            maxId = logs[i].id;
                        }
                    }
                }
                lastLogId = maxId;
            }
        } catch (error) {
            console.error('Error fetching logs:', error);
        }
    }

    const toggleAutoRefresh = document.getElementById('toggle-auto-refresh');
    const refreshStateText = document.getElementById('refresh-state');
    let autoRefreshInterval = setInterval(fetchLogs, 2000);

    if(toggleAutoRefresh) {
        toggleAutoRefresh.addEventListener('change', (e) => {
            if(e.target.checked) {
                refreshStateText.textContent = 'Active';
                refreshStateText.className = 'state-text active';
                addLogEntry('INFO', 'Auto-refresh enabled');
                autoRefreshInterval = setInterval(fetchLogs, 2000);
            } else {
                refreshStateText.textContent = 'Paused';
                refreshStateText.className = 'state-text paused';
                addLogEntry('WARN', 'Auto-refresh disabled');
                clearInterval(autoRefreshInterval);
            }
        });
    }

    // Pre-populate fields
    fetch('/api/config')
        .then(res => res.json())
        .then(data => {
            if (data.wifi && data.wifi.mode === 'STA' && data.wifi.ssid) {
                if (!ssidInput.value) {
                    ssidInput.value = data.wifi.ssid;
                }
            }
            if (data.nut) {
                if (data.nut.username && nutUsernameInput && !nutUsernameInput.value) {
                    nutUsernameInput.value = data.nut.username;
                }
                if (data.nut.ups_name && nutUpsNameInput && !nutUpsNameInput.value) {
                    nutUpsNameInput.value = data.nut.ups_name;
                }
            }
        })
        .catch(err => console.error('Failed to fetch config:', err));
});
