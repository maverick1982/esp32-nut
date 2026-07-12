document.addEventListener('DOMContentLoaded', () => {
    // Tab switching logic
    const tabs = document.querySelectorAll('.tab');
    const contents = document.querySelectorAll('.tab-content');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            tabs.forEach(t => t.classList.remove('active'));
            contents.forEach(c => c.classList.remove('active'));
            
            tab.classList.add('active');
            const target = tab.getAttribute('data-target');
            document.getElementById(`content-${target}`).classList.add('active');
        });
    });

    // Mock Wi-Fi Scanning logic
    const btnScan = document.getElementById('btn-scan');
    const networkList = document.getElementById('network-list');
    const ssidInput = document.getElementById('ssid');
    const pwdInput = document.getElementById('password');
    const btnConnect = document.getElementById('btn-connect');
    const togglePwd = document.getElementById('toggle-pwd');

    const mockNetworks = [
        { ssid: 'Home_IoT_2.4G', sec: 'WPA2', sig: -45, type: 'excellent' },
        { ssid: 'CyberNet_GUEST', sec: 'WPA3', sig: -65, type: 'good' },
        { ssid: 'HiddenNetwork', sec: 'WPA2/Enterprise', sig: -82, type: 'weak' }
    ];

    const getSignalIcon = (type) => {
        return `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" class="signal-${type}"><path d="M5 12.55a11 11 0 0114.08 0M1.42 9a16 16 0 0121.16 0M8.53 16.11a6 6 0 016.95 0M12 20h.01"/></svg>`;
    };

    btnScan.addEventListener('click', () => {
        // Show scanning state
        btnScan.innerHTML = `<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div><span>Scanning</span>`;
        btnScan.disabled = true;
        networkList.innerHTML = `
            <div class="scan-placeholder">
                <div class="spinner"></div>
                <p>Intercepting 802.11 beacons...</p>
            </div>
        `;

        // Simulate network delay
        setTimeout(() => {
            btnScan.innerHTML = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12a9 9 0 11-18 0 9 9 0 0118 0zM12 8v4l3 3"/></svg><span>Rescan</span>`;
            btnScan.disabled = false;
            
            networkList.innerHTML = mockNetworks.map(net => `
                <div class="network-item" data-ssid="${net.ssid}">
                    <div class="net-info">
                        <span class="net-ssid">${net.ssid}</span>
                        <span class="net-meta">CH: ${Math.floor(Math.random()*11)+1} | ${net.sec}</span>
                    </div>
                    <div class="net-signal">
                        <span class="mono">${net.sig}dBm</span>
                        ${getSignalIcon(net.type)}
                    </div>
                </div>
            `).join('');

            // Add click listeners to new items
            document.querySelectorAll('.network-item').forEach(item => {
                item.addEventListener('click', () => {
                    document.querySelectorAll('.network-item').forEach(i => i.classList.remove('selected'));
                    item.classList.add('selected');
                    
                    const ssid = item.getAttribute('data-ssid');
                    ssidInput.value = ssid;
                    
                    pwdInput.disabled = false;
                    pwdInput.focus();
                    togglePwd.disabled = false;
                    btnConnect.disabled = false;
                });
            });
        }, 1500);
    });

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
            
            setTimeout(() => {
                alert('Device successfully connected and configuration saved in NVS.');
            }, 500);
        }, 2000);
    });
});
