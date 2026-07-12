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
});
