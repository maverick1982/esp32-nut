document.addEventListener('DOMContentLoaded', () => {
    // Tab switching logic
    const tabs = document.querySelectorAll('.tab');
    const contents = document.querySelectorAll('.tab-content');
    const pageTitle = document.querySelector('.page-title');

    const titles = {
        'wifi': 'Network Configuration',
        'nut': 'NUT Server Setup',
        'logs': 'System Telemetry & Logs',
        'ota': 'Firmware Update'
    };

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            tabs.forEach(t => t.classList.remove('active'));
            contents.forEach(c => c.classList.remove('active'));
            
            tab.classList.add('active');
            const target = tab.getAttribute('data-target');
            document.getElementById(`content-${target}`).classList.add('active');
            
            if(titles[target]) {
                pageTitle.textContent = titles[target];
            }
        });
    });

    const ssidInput = document.getElementById('ssid');
    const pwdInput = document.getElementById('password');
    const btnConnect = document.getElementById('btn-connect');
    const togglePwd = document.getElementById('toggle-pwd');

    if(togglePwd) {
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
    }

    if(document.getElementById('wifi-form')) {
        // Connect form submission
        document.getElementById('wifi-form').addEventListener('submit', (e) => {
            e.preventDefault();
            btnConnect.innerHTML = `<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div> Handshake...`;
            btnConnect.disabled = true;
            pwdInput.disabled = true;
            
            setTimeout(() => {
                btnConnect.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg> Connected`;
                btnConnect.style.background = 'var(--success)';
                btnConnect.style.borderColor = 'var(--success)';
                btnConnect.style.color = '#000';
                btnConnect.style.boxShadow = '0 0 20px rgba(0, 255, 157, 0.4)';
                
                document.querySelector('.status-indicator').classList.remove('warning');
                document.querySelector('.status-indicator').style.background = 'var(--success)';
                document.querySelector('.status-indicator').style.boxShadow = '0 0 8px rgba(0, 255, 157, 0.5)';
                document.querySelector('.status-indicator').style.animation = 'none';
                document.querySelector('.system-status span').textContent = 'Network Connected';
            }, 2000);
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
            
            setTimeout(() => {
                btnIcon.style.animation = 'none';
                addLogEntry('INFO', 'Manual log refresh completed.');
            }, 500);
        });
    }

    if(btnDownload) {
        btnDownload.addEventListener('click', () => {
            addLogEntry('INFO', 'Downloading logs export file...');
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
        line.innerHTML = `<span class="terminal-time">${timeStr}</span><span class="${levelClass}">[${level}]</span> <span class="terminal-msg">${msg}</span>`;
        
        terminalOutput.appendChild(line);
        terminalOutput.scrollTop = terminalOutput.scrollHeight;
    }

    const toggleAutoRefresh = document.getElementById('toggle-auto-refresh');
    const refreshStateText = document.getElementById('refresh-state');

    if(toggleAutoRefresh) {
        toggleAutoRefresh.addEventListener('change', (e) => {
            if(e.target.checked) {
                refreshStateText.textContent = 'Active';
                refreshStateText.className = 'state-text active';
                addLogEntry('INFO', 'Auto-refresh enabled');
            } else {
                refreshStateText.textContent = 'Paused';
                refreshStateText.className = 'state-text paused';
                addLogEntry('WARN', 'Auto-refresh disabled');
            }
        });
    }

    // Simulate incoming logs randomly
    setInterval(() => {
        if(document.getElementById('content-logs').classList.contains('active') && (!toggleAutoRefresh || toggleAutoRefresh.checked)) {
            if(Math.random() > 0.7) {
                const msgs = [
                    "Client disconnected from 192.168.1.50",
                    "Routine telemetry data sent",
                    "Memory check: 45% heap available",
                    "UPS stats updated: Load 12%"
                ];
                addLogEntry('INFO', msgs[Math.floor(Math.random() * msgs.length)]);
            }
        }
    }, 4000);
});
