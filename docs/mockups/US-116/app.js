// US-116 mockup: fake data + simulated save. Not production code, no network calls.
//
// Shape assumed for GET /api/logs/remote (guarded, ADR 0009):
//   { syslog: { enabled, host, port, format, facility, level, rate,
//               status: { state: "off"|"waiting"|"ready"|"active"|"error",
//                         ip, problem, sent, failed, limited, dropped } } }

// state -> badge / dot / chip text
const LOOK = {
    off:     { badge: 'idle',       dot: 'off',     text: 'Off' },
    waiting: { badge: 'connecting', dot: 'warning', text: 'Waiting' },
    ready:   { badge: 'ap',         dot: 'info',    text: 'Ready' },
    active:  { badge: 'connected',  dot: 'success', text: 'Active' },
    error:   { badge: 'down',       dot: 'danger',  text: 'Error' }
};

const BASE = { enabled: true, port: 514, format: 'rfc3164', facility: 16, level: 'info', rate: 10 };

const STATES = {
    'off': {
        cfg: { enabled: false, host: '', port: 514, format: 'rfc3164', facility: 16, level: 'info', rate: 10 },
        st: { state: 'off' }
    },
    'waiting-net': {
        cfg: { ...BASE, host: '192.168.1.10' },
        st: { state: 'waiting', ip: '', problem: 'No network connection. Lines are queued and sent when the board is back online.', sent: 0, failed: 0, limited: 0, dropped: 0 }
    },
    'waiting-dns': {
        cfg: { ...BASE, host: 'rsyslog.lan' },
        st: { state: 'waiting', ip: '', problem: 'Cannot resolve <strong>rsyslog.lan</strong>. Check the name or use the IP address of the server. Retrying every 30 s.', sent: 0, failed: 0, limited: 0, dropped: 14 }
    },
    'ready': {
        cfg: { ...BASE, host: 'rsyslog.lan' },
        st: { state: 'ready', ip: '192.168.1.10', problem: '', sent: 0, failed: 0, limited: 0, dropped: 0 }
    },
    'active': {
        cfg: { ...BASE, host: '192.168.1.10' },
        st: { state: 'active', ip: '192.168.1.10', problem: '', sent: 1284, failed: 0, limited: 3, dropped: 0 }
    },
    'error': {
        cfg: { ...BASE, host: 'rsyslog.lan', format: 'rfc5424', level: 'warn' },
        st: { state: 'error', ip: '192.168.1.10', problem: 'Send to <strong>192.168.1.10</strong> failed (no route to host). Written once to the System Log, retrying.', sent: 1284, failed: 17, limited: 0, dropped: 0 }
    },
    'invalid': {
        cfg: { enabled: false, host: '', port: 514, format: 'rfc3164', facility: 16, level: 'info', rate: 10 },
        st: { state: 'off' },
        typed: { enabled: true, host: '' },
        error: { field: 'f-host', text: 'Server required to enable syslog' }
    }
};

const NOTES = {
    'off': 'Default after flashing: syslog <b>Off</b>. Neutral dot in the actions bar, idle badge, counters greyed. The form keeps the defaults (port 514, RFC 3164, local0, INFO, 10 lines/s). Turn the switch on and Save with an empty server to see the validation; fill it in to see <b>Ready</b>. Click the indicator next to Auto-refresh: it scrolls to the panel.',
    'waiting-net': '<b>Waiting</b> (amber, pulsing as the sidebar): enabled but the board has no IP yet. The amber notice under the grid explains why; lines written at boot stay in the queue and go out when the link is up.',
    'waiting-dns': '<b>Waiting</b> with a host name that does not resolve. Lines that did not fit in the queue meanwhile are counted in <code>Dropped (not ready)</code>. The indicator keeps the name the user typed; the full reason is in its tooltip and in the panel.',
    'ready': '<b>Ready</b> (blue, as "Access point mode" on the Network page): name resolved, nothing sent yet. The Server cell shows the typed name and, smaller, the resolved address.',
    'active': '<b>Active</b>: the case of the spec demo, <code>Syslog: Active &rarr; 192.168.1.10:514</code>. Counters on the second row of the grid; <code>Rate limited</code> = lines over the per-second limit (a summary line on the server says how many).',
    'error': '<b>Error</b> (red): the server does not answer. The board writes <code>[SYSLOG] ...</code> once per error period in its own terminal (last line), never to the server. UPS and NUT clients keep working.',
    'invalid': 'Client-side check on Save: switch on with an empty server. Field in red + inline alert above Save, nothing sent. A 400 <code>{"error"}</code> of the API uses the same box with its text (e.g. "Invalid port"). Editing the field hides the error.',
    'mobile': 'Below 768px the actions bar is a column (as today): Auto-refresh and the indicator take one full-width row each, buttons two per row. The status grid becomes one row per field (Network page rule), form fields one per row, Save full width. The phones are live.'
};

const HOST_RE = /^[A-Za-z0-9](?:[A-Za-z0-9.-]{0,61}[A-Za-z0-9])?$/;
const $ = (id) => document.getElementById(id);

let cfg = null;       // saved settings
let st = null;        // runtime status
let resultTimer = null;

// ---------- Terminal (fake System Log) ----------
const LINES = [
    ['INFO', '[NET] Wi-Fi connected to HomeNetwork, IP 192.168.1.126'],
    ['INFO', '[USB] UPS attached: Eaton 3S 700 (0463:FFFF)'],
    ['INFO', '[NUT] Server listening on port 3493'],
    ['INFO', "[AUTH] Login succeeded for user 'admin' from 192.168.1.20"],
    ['INFO', '[NUT] Client 192.168.1.40 connected on slot 0'],
    ['WARN', "[AUTH] Login failed for user 'root' from 192.168.1.33"],
    ['INFO', '[UPS] Status OL CHRG, battery 100%']
];
const LINE_SYSLOG_ERR = ['WARN', '[SYSLOG] Send to 192.168.1.10 failed (no route to host), retrying'];
const LINE_SYSLOG_DNS = ['WARN', '[SYSLOG] Cannot resolve rsyslog.lan, retrying'];

function renderTerminal(state) {
    const out = $('terminal-output');
    out.textContent = '';
    const lines = LINES.slice();
    if (state === 'error') lines.push(LINE_SYSLOG_ERR);
    if (state === 'waiting-dns') lines.push(LINE_SYSLOG_DNS);
    const t0 = Date.now() - lines.length * 47000;
    lines.forEach(([level, msg], i) => {
        const d = new Date(t0 + i * 47000);
        const p = (n) => String(n).padStart(2, '0');
        const cls = level === 'WARN' ? 'terminal-level-warn' : level === 'ERROR' ? 'terminal-level-err' : 'terminal-level-info';
        const line = document.createElement('div');
        line.className = 'terminal-line';
        line.innerHTML = `<span class="terminal-time">[${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}]</span>` +
            `<span class="${cls}">[${level}]</span> <span class="terminal-msg"></span>`;
        line.querySelector('.terminal-msg').textContent = msg;
        out.appendChild(line);
    });
}

// ---------- Status: indicator in the actions bar + grid in the panel ----------
function plain(html) { const d = document.createElement('div'); d.innerHTML = html; return d.textContent; }

function renderStatus() {
    const look = LOOK[st.state];
    const off = st.state === 'off';
    const where = `${cfg.host}:${cfg.port}`;

    // Actions bar
    $('syslog-chip-dot').className = 'status-indicator ' + look.dot;
    $('syslog-chip-text').innerHTML = off ? `<b>${look.text}</b>` : `<b>${look.text}</b> &rarr; ${where}`;
    $('syslog-chip').title = st.problem ? plain(st.problem) : (off ? 'Remote logging is off' : 'Remote logging settings');

    // Badge
    const badge = $('syslog-badge');
    badge.className = 'link-badge ' + look.badge;
    badge.querySelector('.status-indicator').className = 'status-indicator ' + look.dot;
    badge.querySelector('.badge-text').textContent = look.text;

    // Server cell: typed name, resolved address in .sub when different
    const tgt = $('syslog-target');
    tgt.className = off ? 'na' : '';
    if (off) tgt.textContent = '—';
    else if (st.ip && st.ip !== cfg.host) tgt.innerHTML = `<span class="main">${cfg.host}:${cfg.port}</span><span class="sub">${st.ip}</span>`;
    else if (!st.ip && cfg.host && !/^\d/.test(cfg.host)) tgt.innerHTML = `<span class="main">${where}</span><span class="sub">not resolved</span>`;
    else tgt.innerHTML = `<span class="main">${where}</span>`;

    // Counters
    [['syslog-sent', 'sent'], ['syslog-failed', 'failed'], ['syslog-limited', 'limited'], ['syslog-dropped', 'dropped']].forEach(([id, k]) => {
        const dd = $(id);
        dd.className = off ? 'na' : '';
        dd.textContent = off ? '—' : st[k].toLocaleString('en-US');
    });

    // Problem text
    const prob = $('syslog-problem');
    prob.hidden = !st.problem;
    prob.className = 'auth-alert' + (st.state === 'waiting' ? ' warn' : '');
    $('syslog-problem-text').innerHTML = st.problem || '';
}

// ---------- Form ----------
function setSwitch(on) {
    $('syslog-on').checked = on;
    const s = $('syslog-on-state');
    s.textContent = on ? 'On' : 'Off';
    s.classList.toggle('on', on);
}

function renderForm(values) {
    setSwitch(values.enabled);
    $('syslog-host').value = values.host;
    $('syslog-port').value = values.port;
    $('syslog-format').value = values.format;
    $('syslog-facility').value = values.facility;
    $('syslog-level').value = values.level;
    $('syslog-rate').value = values.rate;
    updateCount();
    hideError();
    $('syslog-result').textContent = '';
    $('syslog-result').className = 'cmd-result';
}

function updateCount() {
    const n = $('syslog-host').value.length;
    const c = $('syslog-host-count');
    c.textContent = `${n}/63`;
    c.classList.toggle('full', n >= 63);
}

function showError(fieldId, text) {
    hideError();
    $('syslog-error-text').textContent = text;
    $('syslog-error').hidden = false;
    if (fieldId) {
        $(fieldId).classList.add('invalid');
        $(fieldId).querySelector('input').setAttribute('aria-invalid', 'true');
    }
}

function hideError() {
    $('syslog-error').hidden = true;
    ['f-host', 'f-port', 'f-rate'].forEach(id => {
        $(id).classList.remove('invalid');
        $(id).querySelector('input').removeAttribute('aria-invalid');
    });
}

function validate(v) {
    if (v.enabled && !v.host) return ['f-host', 'Server required to enable syslog'];
    if (v.host && !HOST_RE.test(v.host)) return ['f-host', 'Invalid server: use an IP address or a host name (letters, digits, dots, hyphens)'];
    if (!/^\d+$/.test(v.portRaw) || v.port < 1 || v.port > 65535) return ['f-port', 'Invalid port: use 1 to 65535'];
    if (!/^\d+$/.test(v.rateRaw) || v.rate < 1 || v.rate > 100) return ['f-rate', 'Invalid lines per second: use 1 to 100'];
    return null;
}

$('syslog-on').addEventListener('change', (e) => { setSwitch(e.target.checked); hideError(); });
$('syslog-host').addEventListener('input', () => { updateCount(); hideError(); });
['syslog-port', 'syslog-rate'].forEach(id => $(id).addEventListener('input', hideError));

$('syslog-form').addEventListener('submit', (e) => {
    e.preventDefault();
    const v = {
        enabled: $('syslog-on').checked,
        host: $('syslog-host').value.trim(),
        portRaw: $('syslog-port').value.trim(),
        rateRaw: $('syslog-rate').value.trim(),
        format: $('syslog-format').value,
        facility: Number($('syslog-facility').value),
        level: $('syslog-level').value
    };
    v.port = Number(v.portRaw);
    v.rate = Number(v.rateRaw);
    $('syslog-host').value = v.host;

    const err = validate(v);
    if (err) {
        showError(err[0], err[1]);
        $(err[0]).querySelector('input').focus();
        return;
    }

    // simulated POST /api/logs/remote
    const btn = $('btn-save-syslog');
    const label = btn.innerHTML;
    btn.disabled = true;
    btn.innerHTML = '<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div> Saving...';
    setTimeout(() => {
        btn.disabled = false;
        btn.innerHTML = label;
        cfg = { enabled: v.enabled, host: v.host, port: v.port, format: v.format, facility: v.facility, level: v.level, rate: v.rate };
        st = v.enabled
            ? { state: 'ready', ip: /^\d/.test(v.host) ? v.host : '192.168.1.10', problem: '', sent: 0, failed: 0, limited: 0, dropped: 0 }
            : { state: 'off' };
        renderStatus();
        const r = $('syslog-result');
        r.className = 'cmd-result success';
        r.textContent = v.enabled ? `✓ Saved, sending to ${v.host}:${v.port}` : '✓ Saved, syslog off';
        clearTimeout(resultTimer);
        resultTimer = setTimeout(() => r.classList.add('fade'), 4000);
    }, 600);
});

$('btn-rlog-refresh').addEventListener('click', renderStatus);

// Indicator in the actions bar: go to the panel
$('syslog-chip').addEventListener('click', () => {
    const p = $('remote-log');
    p.scrollIntoView({ behavior: 'smooth', block: 'start' });
    p.focus({ preventScroll: true });
});

// ---------- State switch ----------
function applyState(state) {
    document.querySelectorAll('.mock-switcher button').forEach(b =>
        b.classList.toggle('on', b.dataset.state === state));
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

    const s = STATES[state];
    cfg = { ...s.cfg };
    st = { ...s.st };
    renderTerminal(state);
    renderStatus();
    renderForm(cfg);
    if (s.typed) {
        setSwitch(s.typed.enabled);
        $('syslog-host').value = s.typed.host;
        updateCount();
    }
    if (s.error) showError(s.error.field, s.error.text);
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));

// ?embed=1&state=...&scroll=remote|form is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 'off');
const scrollTo = { remote: 'remote-log', form: 'syslog-form' }[params.get('scroll')];
if (scrollTo) window.addEventListener('load', () => $(scrollTo).scrollIntoView({ block: 'start' }));
