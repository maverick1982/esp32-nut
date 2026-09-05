document.addEventListener('DOMContentLoaded', () => {
    const NUT_DESCRIPTIONS = {
    'ups.alarm': 'UPS alarms',
    'ups.status': 'UPS status',
    'ups.time': 'Internal UPS clock time',
    'ups.date': 'Internal UPS clock date',
    'ups.efficiency': 'Efficiency of the UPS',
    'ups.model': 'UPS model',
    'ups.mfr': 'UPS manufacturer',
    'ups.mfr.date': 'UPS manufacturing date',
    'ups.serial': 'UPS serial number',
    'ups.vendorid': 'Vendor ID for USB devices',
    'ups.productid': 'Product ID for USB devices',
    'ups.firmware': 'UPS firmware',
    'ups.firmware.aux': 'Auxiliary device firmware',
    'ups.temperature': 'UPS temperature (degrees C)',
    'ups.load': 'Load on UPS (percent of full)',
    'ups.load.energysave': 'Load on UPS that triggers energysave (percent)',
    'ups.load.high': 'Load when UPS switches to overload condition (percent)',
    'ups.id': 'UPS system identifier',
    'ups.delay.start': 'Interval to wait before (re)starting the load (seconds)',
    'ups.delay.reboot': 'Interval to wait before rebooting the UPS (seconds)',
    'ups.delay.shutdown': 'Interval to wait after shutdown with delay command (seconds)',
    'ups.timer.start': 'Time before the load will be started (seconds)',
    'ups.timer.reboot': 'Time before the load will be rebooted (seconds)',
    'ups.timer.shutdown': 'Time before the load will be shutdown (seconds)',
    'ups.test.interval': 'Interval between self tests (seconds)',
    'ups.test.result': 'Results of last self test',
    'ups.display.language': 'Language to use on front panel',
    'ups.contacts': 'UPS external contact sensors',
    'ups.power': 'Current value of apparent power (VA)',
    'ups.power.nominal': 'UPS power rating (VA)',
    'ups.realpower': 'Current value of real power (W)',
    'ups.realpower.nominal': 'UPS real power rating (W)',
    'ups.beeper.status': 'UPS beeper status',
    'ups.type': 'UPS type',
    'ups.start.auto': 'UPS starts when mains is (re)applied',
    'ups.start.battery': 'Allow to start UPS from battery',
    'ups.start.reboot': 'UPS reboots when power returns during shutdown delay',
    'ups.shutdown': 'Enable or disable UPS shutdown ability (poweroff)',
    'input.voltage': 'Input voltage (V)',
    'input.voltage.extended': 'Extended input voltage range',
    'input.voltage.maximum': 'Maximum incoming voltage seen (V)',
    'input.voltage.minimum': 'Minimum incoming voltage seen (V)',
    'input.voltage.status': 'Voltage status relative to the thresholds',
    'input.voltage.low.warning': 'Input voltage low warning threshold (V)',
    'input.voltage.low.critical': 'Input voltage low critical threshold (V)',
    'input.voltage.high.warning': 'Input voltage high warning threshold (V)',
    'input.voltage.high.critical': 'Input voltage high critical threshold (V)',
    'input.voltage.nominal': 'Nominal input voltage (V)',
    'input.transfer.reason': 'Reason for last transfer to battery',
    'input.transfer.low': 'Low voltage transfer point (V)',
    'input.transfer.high': 'High voltage transfer point (V)',
    'input.transfer.eco.low': 'Low voltage ECO transfer point (V)',
    'input.transfer.bypass.low': 'Low voltage Bypass transfer point (V)',
    'input.transfer.eco.high': 'High voltage ECO transfer point (V)',
    'input.transfer.bypass.high': 'High voltage Bypass transfer point (V)',
    'input.transfer.frequency.bypass.range': 'Frequency range Bypass transfer point (percent of nominal Hz)',
    'input.transfer.frequency.eco.range': 'Frequency range ECO transfer point (percent of nominal Hz)',
    'input.transfer.hysteresis': 'Threshold of switching protection modes, voltage transfer point (V)',
    'input.transfer.low.min': 'smallest settable low voltage transfer point (V)',
    'input.transfer.low.max': 'greatest settable low voltage transfer point (V)',
    'input.transfer.high.min': 'smallest settable high voltage transfer point (V)',
    'input.transfer.high.max': 'greatest settable high voltage transfer point (V)',
    'input.eco.switchable': 'Input High Efficiency (aka ECO) mode switch',
    'input.transfer.bypass.forced': 'Rule for allow auto Bypass switch (on/off) transfer modes (enabled or disabled)',
    'input.transfer.bypass.overload': 'Rule for auto transfer on Bypass when overload (enabled or disabled)',
    'input.transfer.bypass.outlimits': 'Rule for auto transfer on Bypass when out of tolerance (enabled or disabled)',
    'input.bypass.switchable': 'Input auto transfer on Bypass when overload or out of tolerance (enabled or disabled)',
    'input.bypass.switch.on': 'Put the UPS in Bypass mode',
    'input.bypass.switch.off': 'Take the UPS out of Bypass mode',
    'input.bypass.voltage': 'Input bypass voltage (V)',
    'input.bypass.frequency': 'Input bypass frequency (Hz)',
    'input.sensitivity': 'Input power sensitivity',
    'input.quality': 'Input power quality',
    'input.current': 'Input current (A)',
    'input.current.nominal': 'Nominal input current (A)',
    'input.current.status': 'Current status relative to the thresholds',
    'input.current.low.warning': 'Input current low warning threshold (A)',
    'input.current.low.critical': 'Input current low critical threshold (A)',
    'input.current.high.warning': 'Input current high warning threshold (A)',
    'input.current.high.critical': 'Input current high critical threshold (A)',
    'input.frequency': 'Input line frequency (Hz)',
    'input.frequency.extended': 'Extended input frequency range',
    'input.frequency.status': 'Frequency status',
    'input.frequency.nominal': 'Nominal input line frequency (Hz)',
    'input.frequency.low': 'Minimum input line frequency (Hz)',
    'input.frequency.high': 'Maximum input line frequency (Hz)',
    'input.transfer.boost.low': 'Low voltage boosting transfer point (V)',
    'input.transfer.boost.high': 'High voltage boosting transfer point (V)',
    'input.transfer.trim.low': 'Low voltage trimming transfer point (V)',
    'input.transfer.trim.high': 'High voltage trimming transfer point (V)',
    'input.transfer.delay': 'Delay before transfer to mains',
    'input.load': 'Load on (ePDU) input (percent of full)',
    'input.realpower': 'Current sum value of all (ePDU) phases real power (W)',
    'input.power': 'Current sum value of all (ePDU) phases apparent power (VA)',
    'input.source': 'The current input power source',
    'input.source.preferred': 'The preferred input power source',
    'output.voltage': 'Output voltage (V)',
    'output.voltage.nominal': 'Nominal output voltage (V)',
    'output.frequency': 'Output frequency (Hz)',
    'output.frequency.nominal': 'Nominal output frequency (Hz)',
    'output.current': 'Output current (A)',
    'output.current.nominal': 'Nominal output current (A)',
    'battery.charge': 'Battery charge (percent of full)',
    'battery.charge.approx': 'Rough approximation of battery charge',
    'battery.charge.low': 'Remaining battery level when UPS switches to LB (percent)',
    'battery.charge.restart': 'Minimum battery level for restart after power off (percent)',
    'battery.charge.warning': 'Battery level when UPS switches to Warning state (percent)',
    'battery.voltage': 'Battery voltage (V)',
    'battery.current': 'Battery current (A)',
    'battery.capacity': 'Battery capacity (Ah)',
    'battery.temperature': 'Battery temperature (degrees C)',
    'battery.voltage.nominal': 'Nominal battery voltage (V)',
    'battery.runtime': 'Battery runtime (seconds)',
    'battery.runtime.low': 'Remaining battery runtime when UPS switches to LB (seconds)',
    'battery.alarm.threshold': 'Battery alarm threshold',
    'battery.date': 'Battery change date',
    'battery.mfr.date': 'Battery manufacturing date',
    'battery.packs': 'Number of battery packs',
    'battery.packs.bad': 'Number of bad battery packs',
    'battery.type': 'Battery chemistry',
    'battery.protection': 'Prevent deep discharge of battery',
    'battery.energysave': 'Switch off when running on battery and no/low load',
    'battery.energysave.load': 'Switch off UPS if on battery and load level lower (percent)',
    'battery.energysave.delay': 'Delay before switch off UPS if on battery and load level low (min)',
    'battery.energysave.realpower': 'Switch off UPS if on battery and load level lower (Watts)',
    'battery.charger.status': 'Battery charger status',
    'battery.charger.type': 'Type of battery charger',
    'ambient.temperature': 'Ambient temperature (degrees C)',
    'ambient.temperature.alarm': 'Ambient temperature alarm is active',
    'ambient.temperature.status': 'Ambient temperature status relative to the configured thresholds',
    'ambient.temperature.alarm.maximum': 'Maximum allowed ambient temperature (degrees C)',
    'ambient.temperature.alarm.minimum': 'Minimum allowed ambient temperature (degrees C)',
    'ambient.temperature.alarm.enable': 'Enable ambient temperature alarm',
    'ambient.temperature.low': 'Temperature threshold low (degrees C)',
    'ambient.temperature.low.warning': 'Temperature threshold low warning (degrees C)',
    'ambient.temperature.low.critical': 'Temperature threshold low critical (degrees C)',
    'ambient.temperature.high': 'Temperature threshold high (degrees C)',
    'ambient.temperature.high.warning': 'Temperature threshold high warning (degrees C)',
    'ambient.temperature.high.critical': 'Temperature threshold high critical (degrees C)',
    'ambient.humidity': 'Ambient humidity (percent)',
    'ambient.humidity.alarm': 'Ambient humidity alarm is active',
    'ambient.humidity.status': 'Ambient humidity status relative to the configured thresholds',
    'ambient.humidity.alarm.maximum': 'Maximum allowed ambient humidity (percent)',
    'ambient.humidity.alarm.minimum': 'Minimum allowed ambient humidity (percent)',
    'ambient.humidity.alarm.enable': 'Enable ambient humidity alarm',
    'ambient.humidity.low': 'Ambient humidity threshold low (percent)',
    'ambient.humidity.low.warning': 'Ambient humidity threshold low warning (percent)',
    'ambient.humidity.low.critical': 'Ambient humidity threshold low critical (percent)',
    'ambient.humidity.high': 'Ambient humidity threshold high (percent)',
    'ambient.humidity.high.warning': 'Ambient humidity threshold high warning (percent)',
    'ambient.humidity.high.critical': 'Ambient humidity threshold high critical (percent)',
    'ambient.present': 'Ambient sensor presence',
    'ambient.contacts.1.status': 'State of the dry contact sensor 1',
    'ambient.contacts.2.status': 'State of the dry contact sensor 2',
    'outlet.id': 'Outlet system identifier',
    'outlet.desc': 'Outlet description',
    'outlet.switch': 'Outlet switch control',
    'outlet.status': 'Outlet switch status',
    'outlet.protect.status': 'Outlet protection status',
    'outlet.switchable': 'Outlet switch ability',
    'outlet.autoswitch.charge.low': 'Remaining battery level to power off this outlet (percent)',
    'outlet.delay.shutdown': 'Interval to wait before shutting down this outlet (seconds)',
    'outlet.delay.start': 'Interval to wait before restarting this outlet (seconds)',
    'outlet.1.id': 'Outlet system identifier',
    'outlet.1.desc': 'Outlet description',
    'outlet.1.switch': 'Outlet switch control',
    'outlet.1.status': 'Outlet switch status',
    'outlet.1.protect.status': 'Outlet protection status',
    'outlet.1.switchable': 'Outlet switch ability',
    'outlet.1.ecocontrol': 'Master Outlet used to automatically power off the slave outlets',
    'outlet.1.autoswitch.charge.low': 'Remaining battery level to power off this outlet (percent)',
    'outlet.1.delay.shutdown': 'Interval to wait before shutting down this outlet (seconds)',
    'outlet.1.delay.start': 'Interval to wait before restarting this outlet (seconds)',
    'outlet.1.designator': 'Outlet designator',
    'outlet.2.id': 'Outlet system identifier',
    'outlet.2.desc': 'Outlet description',
    'outlet.2.switch': 'Outlet switch control',
    'outlet.2.status': 'Outlet switch status',
    'outlet.2.protect.status': 'Outlet protection status',
    'outlet.2.switchable': 'Outlet switch ability',
    'outlet.2.ecocontrol': 'Master Outlet used to automatically power off the slave outlets',
    'outlet.2.autoswitch.charge.low': 'Remaining battery level to power off this outlet (percent)',
    'outlet.2.delay.shutdown': 'Interval to wait before shutting down this outlet (seconds)',
    'outlet.2.delay.start': 'Interval to wait before restarting this outlet (seconds)',
    'device.part': 'Device part number',
    'device.mfr': 'Device manufacturer',
    'device.model': 'Device model',
    'device.serial': 'Device serial number',
    'device.type': 'Device type',
    'device.description': 'Device description',
    'device.contact': 'Device administrator name',
    'device.location': 'Device physical location',
    'device.macaddr': 'Physical network address of the device',
    'device.uptime': 'Device uptime in seconds',
    'device.count': 'Total number of daisychained devices',
    'device.usb.version': 'Device USB version',
    'server.info': 'Server information',
    'server.version': 'Server version',
    'driver.name': 'Driver name',
    'driver.debug': 'Current debug verbosity level of the driver program',
    'driver.flag.allow_killpower': 'Safety flip-switch to allow the driver daemon to send UPS shutdown command (accessible via driver.killpower)',
    'driver.version': 'Driver version - NUT release',
    'driver.version.internal': 'Internal driver version',
    'driver.version.usb': 'USB library version',
    'driver.version.data': 'Version of the internal data mapping, for generic drivers',
    'driver.state': 'Current state in driver\'s lifecycle',
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
                const sortedKeys = Object.keys(data)
                    .filter(k => k !== 'ups.beeper.switchable' && (!data._disconnected || k !== 'ups.status') && !k.startsWith('_'))
                    .sort();
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
                
                // Remove stale rows that are no longer in the payload
                while (upsTableBody.children.length > currentIndex) {
                    upsTableBody.removeChild(upsTableBody.lastChild);
                }
            }
            
            const beeperActions = document.querySelector('.ups-actions');
            const beeperToggle = document.getElementById('toggle-beeper');
            const beeperState = document.getElementById('beeper-state');
            
            if (beeperActions) {
                if (data['ups.beeper.switchable'] === true && data['ups.beeper.status'] !== undefined) {
                    beeperActions.style.display = 'flex';
                    if (beeperToggle && beeperState) {
                        beeperToggle.disabled = false;
                        const isEnabled = data['ups.beeper.status'] === 'enabled';
                        if (beeperToggle.checked !== isEnabled) {
                            beeperToggle.checked = isEnabled;
                        }
                        beeperState.textContent = isEnabled ? 'Enabled' : 'Disabled';
                        beeperState.className = 'state-text ' + (isEnabled ? 'active' : 'paused');
                    }
                } else {
                    beeperActions.style.display = 'none';
                }
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
