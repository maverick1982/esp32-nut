document.addEventListener('DOMContentLoaded', () => {
    // Tab Switching Logic
    const tabs = document.querySelectorAll('.tab');
    const contents = document.querySelectorAll('.tab-content');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            tabs.forEach(t => t.classList.remove('active'));
            contents.forEach(c => c.classList.remove('active'));
            
            tab.classList.add('active');
            const target = document.getElementById(`content-${tab.dataset.target}`);
            if (target) {
                target.classList.add('active');
            }
        });
    });

    // Mockup Data for UPS Parameters
    const upsParams = [
        { key: 'battery.charge', value: '100', desc: 'Battery charge (percent)' },
        { key: 'battery.voltage', value: '13.6', desc: 'Battery voltage (V)' },
        { key: 'battery.runtime', value: '3240', desc: 'Battery runtime (seconds)' },
        { key: 'input.voltage', value: '230.1', desc: 'Input voltage (V)' },
        { key: 'input.frequency', value: '50.0', desc: 'Input line frequency (Hz)' },
        { key: 'output.voltage', value: '230.1', desc: 'Output voltage (V)' },
        { key: 'ups.load', value: '15', desc: 'Load on UPS (percent)' },
        { key: 'ups.status', value: 'OL', desc: 'UPS status' },
        { key: 'ups.temperature', value: '31.5', desc: 'UPS temperature (C)' },
    ];

    const tableBody = document.getElementById('ups-table-body');
    const elCharge = document.getElementById('ups-charge');
    const barCharge = document.getElementById('bar-charge');
    const elLoad = document.getElementById('ups-load');
    const barLoad = document.getElementById('bar-load');
    const elStatus = document.getElementById('ups-status');

    // Initial render
    function renderTable() {
        tableBody.innerHTML = '';
        upsParams.forEach(param => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td>${param.key}</td>
                <td id="val-${param.key.replace('.', '-')}">${param.value}</td>
                <td>${param.desc}</td>
            `;
            tableBody.appendChild(tr);
        });
    }

    renderTable();

    // Simulate polling every 3 seconds
    setInterval(() => {
        // Small random fluctuations to simulate live data
        upsParams.forEach(param => {
            if (param.key === 'input.voltage' || param.key === 'output.voltage') {
                const fluctuation = (Math.random() * 2 - 1).toFixed(1);
                param.value = (230.0 + parseFloat(fluctuation)).toFixed(1);
            }
            if (param.key === 'ups.load') {
                const fluctuation = Math.floor(Math.random() * 3 - 1); // -1, 0, or 1
                let newLoad = parseInt(param.value) + fluctuation;
                newLoad = Math.max(0, Math.min(100, newLoad));
                param.value = newLoad.toString();
            }
            if (param.key === 'ups.temperature') {
                const fluctuation = (Math.random() * 0.4 - 0.2).toFixed(1);
                param.value = (parseFloat(param.value) + parseFloat(fluctuation)).toFixed(1);
            }

            // Update DOM table
            const cell = document.getElementById(`val-${param.key.replace('.', '-')}`);
            if (cell && cell.innerText !== param.value) {
                cell.innerText = param.value;
                // Add flash effect
                cell.parentElement.classList.remove('flash');
                void cell.parentElement.offsetWidth; // trigger reflow
                cell.parentElement.classList.add('flash');
            }
        });

        // Update primary metrics UI
        const currentLoad = upsParams.find(p => p.key === 'ups.load').value;
        const currentCharge = upsParams.find(p => p.key === 'battery.charge').value;
        
        elLoad.innerText = currentLoad;
        barLoad.style.width = `${currentLoad}%`;

        elCharge.innerText = currentCharge;
        barCharge.style.width = `${currentCharge}%`;

        // Update color for load
        if (parseInt(currentLoad) > 80) {
            barLoad.style.background = 'var(--danger)';
            barLoad.style.boxShadow = '0 0 10px var(--danger)';
        } else {
            barLoad.style.background = 'var(--accent)';
            barLoad.style.boxShadow = '0 0 10px var(--accent)';
        }
        
    }, 3000);
});
