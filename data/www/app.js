document.addEventListener('DOMContentLoaded', () => {
    const NUT_DESCRIPTIONS = {
        'ups.status': 'UPS status',
        'ups.mfr': 'UPS manufacturer',
        'ups.model': 'UPS model',
        'ups.serial': 'UPS serial number',
        'ups.mfr.date': 'UPS manufacturing date',
        'battery.charge': 'Battery charge (percent)',
        'battery.charge.low': 'Remaining battery level when UPS switches to LB (percent)',
        'battery.capacity': 'Battery design capacity (Ah)',
        'battery.capacity.full': 'Battery full charge capacity (Ah)',
        'battery.runtime': 'Battery runtime (seconds)',
        'battery.voltage': 'Battery voltage (V)',
        'battery.temperature': 'Battery temperature (degrees C)',
        'battery.mfr.date': 'Battery manufacturing date',
        'battery.date': 'Battery replacement date',
        'input.voltage': 'Input voltage (V)',
        'output.voltage': 'Output voltage (V)',
        'input.transfer.high': 'High voltage transfer point (V)',
        'input.transfer.low': 'Low voltage transfer point (V)',
        'ups.power.nominal': 'UPS apparent power rating (VA)',
        'ups.realpower.nominal': 'UPS real power rating (W)',
        'input.frequency.nominal': 'Nominal input line frequency (Hz)',
        'input.voltage.nominal': 'Nominal input line voltage (V)',
        'output.voltage.nominal': 'Nominal output voltage (V)',
        'output.frequency.nominal': 'Nominal output frequency (Hz)',
        'ups.load': 'Load on UPS (percent)',
        'ups.realpower': 'Current value of real power (W)',
        'ups.delay.shutdown': 'Interval to wait after shutdown with delay command (seconds)',
        'ups.delay.start': 'Interval to wait before (re)starting the load (seconds)',
        'ups.timer.start': 'Time before the load will be started (seconds)',
        'ups.timer.shutdown': 'Time before the load will be shutdown (seconds)',
        'battery.type': 'Battery chemistry',
        'ups.type': 'UPS type',
        'ups.beeper.status': 'UPS beeper status',
        'outlet.1.switch': 'Outlet 1 switch status',
        'outlet.2.switch': 'Outlet 2 switch status'
    };

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

    const linkExportUsb = document.getElementById('link-export-usb');
    if(linkExportUsb) {
        linkExportUsb.addEventListener('click', () => {
            addLogEntry('INFO', 'Downloading USB diagnostics...');
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

    const toggleBeeper = document.getElementById('toggle-beeper');
    if (toggleBeeper) {
        toggleBeeper.addEventListener('change', async (e) => {
            const isEnabled = e.target.checked;
            toggleBeeper.disabled = true;
            try {
                const res = await fetch('/api/beeper', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ enable: isEnabled })
                });
                if (!res.ok) {
                    toggleBeeper.checked = !isEnabled;
                }
            } catch (err) {
                console.error('Beeper API error:', err);
                toggleBeeper.checked = !isEnabled;
            }
            toggleBeeper.disabled = false;
            fetchUpsVars();
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

    // UPS Parameters Logic
    const upsTableBody = document.getElementById('ups-table-body');
    const elCharge = document.getElementById('ups-charge');
    const barCharge = document.getElementById('bar-charge');
    const elLoad = document.getElementById('ups-load');
    const barLoad = document.getElementById('bar-load');
    const elStatus = document.getElementById('ups-status');
    const elRealPower = document.getElementById('ups-realpower');

    let upsPollInterval = null;

    async function fetchUpsVars() {
        if (!document.getElementById('content-ups').classList.contains('active')) return;
        
        try {
            const response = await fetch('/api/ups-vars');
            if (!response.ok) throw new Error('Network response was not ok');
            const data = await response.json();
            
            if (data.error) {
                console.error('UPS Error:', data.error);
                return;
            }

            const genericBanner = document.getElementById('generic-ups-banner');
            if (genericBanner) {
                if (data['ups.type'] === 'Generic') {
                    genericBanner.style.display = 'block';
                } else {
                    genericBanner.style.display = 'none';
                }
            }


            // Update primary metrics
            if (elStatus) elStatus.innerText = data['ups.status'] || '--';
            
            if (elCharge) {
                const charge = data['battery.charge'] || 0;
                elCharge.innerText = charge;
                if (barCharge) barCharge.style.width = `${charge}%`;
            }
            
            if (elLoad) {
                const load = data['ups.load'] || 0;
                elLoad.innerText = load;
                if (barLoad) {
                    barLoad.style.width = `${load}%`;
                    if (parseInt(load) > 80) {
                        barLoad.style.background = 'var(--danger)';
                        barLoad.style.boxShadow = '0 0 10px var(--danger)';
                    } else {
                        barLoad.style.background = 'var(--accent)';
                        barLoad.style.boxShadow = '0 0 10px var(--accent)';
                    }
                }
            }

            if (elRealPower) {
                const realPower = data['ups.realpower'] || '--';
                elRealPower.innerText = realPower;
            }

            // Update table
            if (upsTableBody) {
                const sortedKeys = Object.keys(data).sort();
                let currentIndex = 0;
                
                for (const key of sortedKeys) {
                    const value = data[key];
                    const rowId = `row-${key.replace(/\./g, '-')}`;
                    let tr = document.getElementById(rowId);
                    
                    if (!tr) {
                        const desc = NUT_DESCRIPTIONS[key] || '--';
                        tr = document.createElement('tr');
                        tr.id = rowId;
                        tr.innerHTML = `
                            <td>${key}</td>
                            <td class="ups-val">${value}</td>
                            <td>${desc}</td>
                        `;
                    } else {
                        const valCell = tr.querySelector('.ups-val');
                        if (valCell && valCell.textContent !== String(value)) {
                            valCell.textContent = value;
                        }
                    }
                    
                    // Enforce DOM order
                    if (upsTableBody.children[currentIndex] !== tr) {
                        upsTableBody.insertBefore(tr, upsTableBody.children[currentIndex] || null);
                    }
                    currentIndex++;
                }
            }
            
            const beeperToggle = document.getElementById('toggle-beeper');
            const beeperState = document.getElementById('beeper-state');
            if (beeperToggle && beeperState && data['ups.beeper.status'] !== undefined) {
                beeperToggle.disabled = false;
                const isEnabled = data['ups.beeper.status'] === 'enabled';
                if (beeperToggle.checked !== isEnabled) {
                    beeperToggle.checked = isEnabled;
                }
                beeperState.textContent = isEnabled ? 'Enabled' : 'Disabled';
                beeperState.className = 'state-text ' + (isEnabled ? 'active' : 'paused');
            }
            
        } catch (error) {
            console.error('Failed to fetch UPS vars:', error);
        }
    }

    // Start polling when UPS tab is clicked
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            if (tab.getAttribute('data-target') === 'ups') {
                if (!upsPollInterval) {
                    fetchUpsVars();
                    upsPollInterval = setInterval(fetchUpsVars, 2000);
                }
            } else {
                if (upsPollInterval) {
                    clearInterval(upsPollInterval);
                    upsPollInterval = null;
                }
            }
        });
    });

    // Start polling immediately if UPS tab is active on load
    if (activeTab && activeTab.getAttribute('data-target') === 'ups') {
        if (!upsPollInterval) {
            fetchUpsVars();
            upsPollInterval = setInterval(fetchUpsVars, 2000);
        }
    }

    // System Status Polling
    async function fetchSystemStatus() {
        try {
            const response = await fetch('/api/system-status');
            if (!response.ok) return;
            const data = await response.json();
            
            const indWifi = document.getElementById('ind-wifi');
            const lblWifi = document.getElementById('lbl-wifi');
            const indUps = document.getElementById('ind-ups');
            const lblUps = document.getElementById('lbl-ups');

            if (lblWifi && data.wifi) {
                lblWifi.textContent = 'Wi-Fi: ' + data.wifi.status;
                if (data.wifi.status === 'AP Mode Active') {
                    indWifi.className = 'status-indicator info';
                } else if (data.wifi.status === 'Connecting') {
                    indWifi.className = 'status-indicator warning';
                } else if (data.wifi.status === 'Disconnected') {
                    indWifi.className = 'status-indicator danger';
                } else {
                    indWifi.className = 'status-indicator success';
                }
            }

            if (lblUps && data.ups) {
                lblUps.textContent = 'UPS: ' + data.ups.status;
                if (data.ups.status === 'Connecting') {
                    indUps.className = 'status-indicator warning';
                } else if (data.ups.status === 'Disconnected') {
                    indUps.className = 'status-indicator danger';
                } else {
                    indUps.className = 'status-indicator success';
                }
            }
            
            if (data.version) {
                const fwVersion = document.getElementById('fw-version');
                if (fwVersion) fwVersion.textContent = data.version;
            }
        } catch (error) {
            console.error('Failed to fetch system status:', error);
            const indWifi = document.getElementById('ind-wifi');
            const lblWifi = document.getElementById('lbl-wifi');
            const indUps = document.getElementById('ind-ups');
            const lblUps = document.getElementById('lbl-ups');
            
            if (lblWifi) lblWifi.textContent = 'Wi-Fi: Offline';
            if (indWifi) indWifi.className = 'status-indicator danger';
            if (lblUps) lblUps.textContent = 'UPS: Offline';
            if (indUps) indUps.className = 'status-indicator danger';
        }
    }

    fetchSystemStatus();
    setInterval(fetchSystemStatus, 3000);

    const exportUsbBtn = document.getElementById('link-export-usb');
    if (exportUsbBtn) {
        exportUsbBtn.addEventListener('click', async (e) => {
            if (exportUsbBtn.classList.contains('disabled')) {
                e.preventDefault();
                return;
            }
            e.preventDefault();
            const originalHTML = exportUsbBtn.innerHTML;
            const currentWidth = exportUsbBtn.offsetWidth;
            
            exportUsbBtn.style.width = currentWidth + 'px';
            exportUsbBtn.classList.add('disabled');
            exportUsbBtn.style.pointerEvents = 'none';
            exportUsbBtn.innerHTML = `
                <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"></path><polyline points="14 2 14 8 20 8"></polyline><line x1="12" y1="18" x2="12" y2="12"></line><line x1="9" y1="15" x2="15" y2="15"></line></svg>
                Exporting...
            `;
            
            try {
                const response = await fetch(exportUsbBtn.href);
                if (response.ok) {
                    const blob = await response.blob();
                    const url = window.URL.createObjectURL(blob);
                    const a = document.createElement('a');
                    a.href = url;
                    a.download = exportUsbBtn.getAttribute('download') || 'usb_diagnostics.json';
                    document.body.appendChild(a);
                    a.click();
                    window.URL.revokeObjectURL(url);
                    a.remove();
                } else {
                    console.error('Export failed with status:', response.status);
                }
            } catch (err) {
                console.error('Export failed:', err);
            } finally {
                exportUsbBtn.classList.remove('disabled');
                exportUsbBtn.style.pointerEvents = 'auto';
                exportUsbBtn.style.width = '';
                exportUsbBtn.innerHTML = originalHTML;
            }
        });
    }
});
