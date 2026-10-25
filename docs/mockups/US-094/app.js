// US-094 mockup — fake data + simulated interactions. Not production code.

// Shape assumed for GET /api/board:
// { board, led: { present, enabled, level } | null, usb: { ports: [{ id, label, meta }], port } | null }
// The LED section appears only with a LED in the profile, the USB section only with 2+ USB host ports.
const LEVELS = [3, 5, 10, 20, 40];          // setBrightness values for levels 1..5 (each doubles the previous)
const PORTS = [
    { id: 'hs', label: 'High-Speed', meta: 'USB 2.0 HS · 480 Mbit/s' },
    { id: 'fs', label: 'Full-Speed', meta: 'USB 1.1 FS · 12 Mbit/s' }
];
const BOARDS = {
    s3: { board: 'esp32s3_devkit', label: 'ESP32-S3 DevKitC-1', net: 'Wi-Fi', led: true, ports: null },
    dual: { board: 'esp32p4_dual_usb_test', label: 'ESP32-P4 dual USB (test profile)', net: 'Wi-Fi', led: true, ports: PORTS },
    noled: { board: 'waveshare_p4_eth', label: 'Waveshare ESP32-P4-ETH', net: 'Ethernet', led: false, ports: PORTS }
};
const RESTART_S = 15;
const RESULT_MS = 4000;

const ICON_SPIN = '<span class="cmd-spinner"></span>';

let st = null;            // current "device" state
let countdownTimer = null;
let fadeTimer = null;

const $ = (id) => document.getElementById(id);
const levelBtns = () => [...document.querySelectorAll('#level-picker .level')];

// ---------- Render ----------
function renderBoard() {
    // US-094: the readable label of the profile; the profile id stays in the tooltip
    $('board-name').textContent = st.label;
    $('board-name').parentElement.title = 'Board profile: ' + st.board + ' (read-only)';
    $('sec-led').hidden = !st.led;
    $('sec-usb').hidden = !st.ports;
    $('lbl-net').textContent = `${st.net}: Connected`;
    if (st.led) renderLed();
    if (st.ports) renderUsb();
}

function renderLed() {
    $('led-enabled').checked = st.enabled;
    $('led-state').textContent = st.enabled ? 'On' : 'Off';
    $('led-state').classList.toggle('on', st.enabled);

    const row = $('row-brightness');
    row.classList.toggle('is-disabled', !st.enabled);
    levelBtns().forEach(b => {
        const n = +b.dataset.level;
        b.setAttribute('aria-checked', String(n === st.level));
        b.classList.toggle('lit', n <= st.level);
        b.disabled = !st.enabled;
    });
    $('level-readout').textContent = st.enabled
        ? `Level ${st.level}`
        : `LED off · level ${st.level} kept`;

    // Preview dot: perceptual glow 0.2 .. 1
    const pv = $('led-preview');
    pv.classList.toggle('off', !st.enabled);
    pv.style.setProperty('--glow', (0.2 + (st.level - 1) * 0.2).toFixed(2));
}

function renderUsb() {
    document.querySelectorAll('input[name="usb-port"]').forEach(r => {
        r.checked = r.value === st.port;
        r.disabled = false;
    });
    document.querySelectorAll('.port-current').forEach(t => { t.hidden = t.dataset.port !== st.port; });
    $('btn-usb').disabled = true;
    setUsbButton('Save &amp; restart');
    $('usb-error').hidden = true;
    $('usb-success').hidden = true;
    document.querySelector('.port-options').classList.remove('invalid');
}

// ---------- LED: applied immediately ----------
function showLedResult(ok, html, sticky) {
    const out = $('led-result');
    clearTimeout(fadeTimer);
    out.className = 'cmd-result ' + (ok ? 'success' : 'error');
    out.innerHTML = (ok ? '&#10003; ' : '&#10005; ') + html;
    if (ok && !sticky) {
        fadeTimer = setTimeout(() => {
            out.classList.add('fade');
            setTimeout(() => { out.className = 'cmd-result'; out.textContent = ''; }, 600);
        }, RESULT_MS);
    }
}

function clearLedError() {
    $('led-error').hidden = true;
    $('level-picker').classList.remove('invalid');
}

function showLedError(text) {
    $('led-error-text').textContent = text;
    $('led-error').hidden = false;
    $('level-picker').classList.add('invalid');
    $('led-result').className = 'cmd-result';
    $('led-result').textContent = '';
}

$('led-enabled').addEventListener('change', (e) => {
    const on = e.target.checked;
    clearLedError();
    e.target.disabled = true;                // simulated POST /api/board { led_enabled }
    setTimeout(() => {
        e.target.disabled = false;
        st.enabled = on;
        renderLed();
        showLedResult(true, on ? 'LED on · saved' : 'LED off · saved, stays dark in every state');
    }, 450);
});

$('level-picker').addEventListener('click', (e) => {
    const btn = e.target.closest('.level');
    if (!btn || btn.disabled) return;
    const level = +btn.dataset.level;
    if (level === st.level) return;
    clearLedError();
    const old = st.level;
    levelBtns().forEach(b => { b.disabled = true; });
    btn.insertAdjacentHTML('beforeend', ICON_SPIN);
    setTimeout(() => {                       // simulated POST /api/board { led_level }
        btn.querySelector('.cmd-spinner')?.remove();
        st.level = level;
        renderLed();
        showLedResult(true, `Applied <span class="arrow">level ${old} &rarr;</span> ${level}`);
    }, 500);
});

// Arrow keys move along the radiogroup
$('level-picker').addEventListener('keydown', (e) => {
    if (!['ArrowLeft', 'ArrowRight'].includes(e.key) || !st.enabled) return;
    e.preventDefault();
    const next = Math.min(5, Math.max(1, st.level + (e.key === 'ArrowRight' ? 1 : -1)));
    const b = levelBtns()[next - 1];
    b.focus();
    b.click();
});

// ---------- USB port: saved, then the board restarts ----------
function setUsbButton(html, spinner) {
    $('btn-usb').innerHTML = '<span class="glow"></span>' + (spinner ? ICON_SPIN : '') +
        `<span class="btn-label">${html}</span>`;
}

function selectedPort() {
    return document.querySelector('input[name="usb-port"]:checked').value;
}

document.querySelectorAll('input[name="usb-port"]').forEach(r => r.addEventListener('change', () => {
    $('usb-error').hidden = true;
    document.querySelector('.port-options').classList.remove('invalid');
    $('btn-usb').disabled = selectedPort() === st.port;
}));

function startRestart(live) {
    const port = selectedPort();
    document.querySelectorAll('input[name="usb-port"]').forEach(r => { r.disabled = true; });
    $('btn-usb').disabled = true;
    setUsbButton('Restarting…', true);
    $('usb-success').hidden = false;
    $('ind-ups').className = 'status-indicator warning';
    $('lbl-ups').textContent = 'Restarting...';
    let left = RESTART_S;
    $('usb-countdown').textContent = left;
    clearInterval(countdownTimer);
    if (!live) { $('usb-countdown').textContent = 12; return; }
    countdownTimer = setInterval(() => {
        left -= 1;
        $('usb-countdown').textContent = left;
        if (left <= 0) {                      // simulated reload after the restart
            clearInterval(countdownTimer);
            st.port = port;
            $('ind-ups').className = 'status-indicator success';
            $('lbl-ups').textContent = 'UPS: Connected';
            renderUsb();
        }
    }, 1000);
}

$('usb-form').addEventListener('submit', (e) => {
    e.preventDefault();
    if ($('btn-usb').disabled) return;
    $('btn-usb').disabled = true;
    setUsbButton('Saving…', true);
    setTimeout(() => startRestart(true), 800);   // simulated POST /api/board { usb_port }
});

function showUsbError(text) {
    $('usb-error-text').textContent = text;
    $('usb-error').hidden = false;
    document.querySelector('.port-options').classList.add('invalid');
    setUsbButton('Save &amp; restart');
    $('btn-usb').disabled = false;
}

// ---------- Mockup state switcher ----------
const NOTES = {
    's3': 'ESP32-S3 DevKit: LED section only (single USB peripheral, no port choice). Switch and brightness are <b>applied immediately</b>, no Save button. Click a level: the segment spins, then <b>Applied level x &rarr; y</b> fades after 4 s.',
    's3-off': 'LED off: the brightness row stays visible with its saved level but is greyed and not clickable. The preview dot next to <code>LED</code> goes dark.',
    's3-applied': 'Level 3 &rarr; 4: outcome kept here for inspection; in the product it fades like the US-068 results.',
    's3-error': 'Server answered <code>400 {"error":"Brightness level must be 1-5"}</code>: red frame on the picker and inline alert, the previous level stays selected. Cleared by the next change.',
    'dual': 'Two USB host ports + LED (fake e2e profile). The USB section is a <b>form</b>: pick a port, <b>Save &amp; restart</b> turns on only when it differs from the port <b>In use</b>. Try it: a 15 s countdown, then the page "reloads".',
    'dual-restart': 'After Save: controls locked, button <b>Restarting…</b>, success alert with the countdown (frozen at 12 here), sidebar UPS indicator goes amber.',
    'dual-error': '<code>400 {"error":"Invalid USB port for this board"}</code>: inline alert above the button, selected card framed red. Nothing is saved, no restart.',
    'noled': 'Waveshare ESP32-P4-ETH: no LED in the profile, so the <b>Status LED section is absent</b>; only the USB port. Sidebar shows Ethernet.',
    'mobile': 'Below 768px: sidebar on top (existing US-042 layout), the LED switch stays on one line, the brightness picker takes the full width, port cards stack and the Save button is full width. The phones are live.'
};

function fresh(kind) {
    const b = BOARDS[kind];
    return { ...b, enabled: true, level: 3, port: b.ports ? 'hs' : null };
}

function applyState(state) {
    document.querySelectorAll('.mock-switcher button').forEach(btn =>
        btn.classList.toggle('on', btn.dataset.state === state));
    $('mock-note').innerHTML = NOTES[state] || '';

    const stage = $('phone-stage');
    const app = $('app');
    if (state === 'mobile') {
        stage.hidden = false;
        app.hidden = true;
        stage.querySelectorAll('iframe').forEach(f => { if (!f.src) f.src = f.dataset.src; });
        return;
    }
    stage.hidden = true;
    app.hidden = false;
    clearInterval(countdownTimer);
    clearTimeout(fadeTimer);

    const kind = state.split('-')[0];
    st = fresh(kind);
    $('ind-ups').className = 'status-indicator success';
    $('lbl-ups').textContent = 'UPS: Connected';
    $('led-result').className = 'cmd-result';
    $('led-result').textContent = '';
    clearLedError();

    if (state === 's3-off') st.enabled = false;
    if (state === 's3-applied') st.level = 4;
    renderBoard();

    if (state === 's3-applied') showLedResult(true, 'Applied <span class="arrow">level 3 &rarr;</span> 4', true);
    if (state === 's3-error') showLedError('Brightness level must be 1-5');
    if (state === 'dual-restart' || state === 'noled-restart') {
        document.querySelector('input[name="usb-port"][value="fs"]').checked = true;
        startRestart(false);
    }
    if (state === 'dual-error') {
        document.querySelector('input[name="usb-port"][value="fs"]').checked = true;
        showUsbError('Invalid USB port for this board');
    }
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));

// ?embed=1&state=...&scroll=1 is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 's3');
if (params.get('scroll') === '1') {
    window.addEventListener('load', () => $('content-board').scrollIntoView({ block: 'start' }));
}
