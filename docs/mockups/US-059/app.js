// US-059 mockup — fake data + simulated interactions. Not production code.

// Shape suggested for GET /api/ups/commands: [{ name, desc, destructive }]
const CATALOG = [
    { name: 'test.battery.start.quick', desc: 'Start a quick battery test', destructive: false },
    { name: 'test.battery.start.deep', desc: 'Start a deep battery test', destructive: false },
    { name: 'test.battery.stop', desc: 'Stop the battery test', destructive: false },
    { name: 'test.panel.start', desc: 'Start testing the UPS panel', destructive: false },
    { name: 'test.panel.stop', desc: 'Stop a UPS panel test', destructive: false },
    { name: 'beeper.enable', desc: 'Enable the UPS beeper', destructive: false },
    { name: 'beeper.disable', desc: 'Disable the UPS beeper', destructive: false },
    { name: 'beeper.mute', desc: 'Temporarily mute the UPS beeper', destructive: false },
    { name: 'beeper.toggle', desc: 'Toggle the UPS beeper', destructive: false },
    { name: 'load.off', desc: 'Turn off the load immediately', destructive: true },
    { name: 'load.on', desc: 'Turn on the load immediately', destructive: true },
    { name: 'load.off.delay', desc: 'Turn off the load with a delay (seconds)', destructive: true },
    { name: 'load.on.delay', desc: 'Turn on the load with a delay (seconds)', destructive: true },
    { name: 'shutdown.return', desc: 'Turn off the load and return when power is back', destructive: true },
    { name: 'shutdown.stayoff', desc: 'Turn off the load and remain off', destructive: true },
    { name: 'shutdown.stop', desc: 'Stop a shutdown in progress', destructive: true },
    { name: 'shutdown.reboot', desc: 'Shut down the load briefly while rebooting the UPS', destructive: true }
];

// Simulated POST /api/ups/command outcomes (200 by default)
const FAKE_ERRORS = {
    'test.panel.start': 'Command rejected by the UPS'
};

const ICON_PLAY = '<svg viewBox="0 0 24 24" fill="currentColor" stroke="none"><path d="M7 4l13 8-13 8z"/></svg>';
const ICON_LOCK = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0110 0v4"/></svg>';
const ICON_ZAP = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9z"/></svg>';
const ICON_FLASK = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M9 3h6M10 3v6L4 20a1 1 0 001 1h14a1 1 0 001-1L14 9V3"/></svg>';
const LOCK_NOTE = 'Available via NUT only until web authentication is implemented';

function esc(s) {
    return String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

function rowHtml(cmd) {
    const locked = cmd.destructive;
    return `
        <div class="cmd-row" data-cmd="${esc(cmd.name)}">
            <div class="cmd-name">${esc(cmd.name)}</div>
            <div class="cmd-desc">${esc(cmd.desc)}</div>
            <div class="cmd-action">
                <span class="cmd-result" aria-live="polite"></span>
                <button type="button" class="btn btn-outline btn-sm cmd-run"
                    ${locked ? `disabled title="${LOCK_NOTE}"` : ''}>
                    ${locked ? ICON_LOCK : ICON_PLAY} Run
                </button>
            </div>
        </div>`;
}

function groupHtml(title, icon, cmds, locked) {
    if (!cmds.length) return '';
    return `
        <section class="cmd-group${locked ? ' locked' : ''}" id="${locked ? 'cmd-group-power' : 'cmd-group-actions'}">
            <div class="cmd-group-header">${icon}<span>${title}</span>
                <span class="cmd-group-count">${cmds.length}</span></div>
            ${locked ? `<div class="cmd-group-note">${ICON_LOCK}${LOCK_NOTE}</div>` : ''}
            ${cmds.map(rowHtml).join('')}
        </section>`;
}

function renderList(catalog) {
    const list = document.getElementById('cmd-list');
    const empty = document.getElementById('cmd-empty');
    if (!catalog.length) {
        list.innerHTML = '';
        empty.hidden = false;
        return;
    }
    empty.hidden = true;
    list.innerHTML =
        groupHtml('Tests &amp; beeper', ICON_FLASK, catalog.filter(c => !c.destructive), false) +
        groupHtml('Power control', ICON_ZAP, catalog.filter(c => c.destructive), true);
}

function setTestResult(text, kind) {
    const box = document.getElementById('cmd-test-result');
    if (!text) { box.hidden = true; return; }
    box.hidden = false;
    box.className = 'test-result' + (kind === 'running' ? ' running' : kind === 'failed' ? ' failed' : '');
    document.getElementById('cmd-test-result-dot').className =
        'status-indicator ' + (kind === 'running' ? 'info' : kind === 'failed' ? 'danger' : 'success');
    document.getElementById('cmd-test-result-value').textContent = text;
}

let testTimer = null;

document.getElementById('cmd-list').addEventListener('click', (e) => {
    const btn = e.target.closest('.cmd-run');
    if (!btn || btn.disabled) return;
    const row = btn.closest('.cmd-row');
    const name = row.dataset.cmd;
    const out = row.querySelector('.cmd-result');

    out.className = 'cmd-result';
    out.textContent = '';
    btn.disabled = true;
    btn.classList.add('running');
    btn.innerHTML = '<span class="cmd-spinner"></span> Running…';

    setTimeout(() => {          // simulated fetch('/api/ups/command', { method: 'POST' })
        btn.disabled = false;
        btn.classList.remove('running');
        btn.innerHTML = ICON_PLAY + ' Run';

        const err = FAKE_ERRORS[name];
        out.className = 'cmd-result ' + (err ? 'error' : 'success');
        out.textContent = err ? '✕ ' + err : '✓ Command sent';
        setTimeout(() => out.classList.add('fade'), 4000);
        setTimeout(() => { out.className = 'cmd-result'; out.textContent = ''; }, 4700);

        if (!err && name.startsWith('test.battery.start')) {
            clearTimeout(testTimer);
            setTestResult('In progress', 'running');
            testTimer = setTimeout(() => setTestResult('Done and passed', 'ok'), 6000);
        }
        if (!err && name === 'test.battery.stop') {
            clearTimeout(testTimer);
            setTestResult('Aborted', 'failed');
        }
    }, 1200);
});

// --- Mockup state switcher ---
const METRICS_ON = { status: 'OL', charge: 100, load: 18, power: 72 };

function setMetrics(m) {
    const st = document.getElementById('cmd-ups-status');
    st.textContent = m ? m.status : '--';
    st.className = 'metric-value' + (m ? ' status-online' : '');
    document.getElementById('cmd-ups-charge').textContent = m ? m.charge : '--';
    document.getElementById('cmd-ups-load').textContent = m ? m.load : '--';
    document.getElementById('cmd-ups-realpower').textContent = m ? m.power : '--';
    document.getElementById('cmd-bar-charge').style.width = (m ? m.charge : 0) + '%';
    document.getElementById('cmd-bar-load').style.width = (m ? m.load : 0) + '%';
    document.getElementById('ind-ups').className = 'status-indicator ' + (m ? 'success' : 'danger');
    document.getElementById('lbl-ups').textContent = m ? 'UPS: Connected' : 'UPS: Disconnected';
}

function applyState(state) {
    clearTimeout(testTimer);
    document.querySelectorAll('.mock-switcher button').forEach(b =>
        b.classList.toggle('on', b.dataset.state === state));
    if (state === 'empty') {
        setMetrics(null);
        setTestResult(null);
        renderList([]);
    } else {
        setMetrics(METRICS_ON);
        setTestResult(state === 'no-test' ? null : 'Done and passed', 'ok');
        renderList(CATALOG);
    }
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));

applyState('populated');
