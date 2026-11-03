// US-112 mockup — fake data + simulated refresh. Not production code.

// Which sections exist comes from GET /api/config (links the board has), e.g. { links: ['wifi'] }.
// Status comes from GET /api/system-status -> network:
// { type, state, ip, gateway, mac, dhcp, ssid, rssi, link_speed }
// state: 'connected' | 'connecting' | 'disconnected' | 'ap'
const BOARDS = {
    s3: { links: ['wifi'], id: '3C8427C12AF0', net: 'Wi-Fi' },
    p4: { links: ['ethernet'], id: '30EDA0E14B7C', net: 'Ethernet' }
};

const STATES = {
    's3-connected': {
        board: 's3',
        network: { type: 'wifi', state: 'connected', ip: '192.168.1.126', gateway: '192.168.1.1',
            mac: '3C:84:27:C1:2A:F0', dhcp: true, ssid: 'HomeNetwork', rssi: -58 }
    },
    's3-ap': {
        board: 's3',
        network: { type: 'wifi', state: 'ap', ip: '192.168.4.1', gateway: '',
            mac: '3C:84:27:C1:2A:F1', dhcp: true, ssid: 'ESP32-NUT-2AF0', rssi: null }
    },
    's3-connecting': {
        board: 's3',
        network: { type: 'wifi', state: 'connecting', ip: '', gateway: '',
            mac: '3C:84:27:C1:2A:F0', dhcp: true, ssid: 'HomeNetwork', rssi: null }
    },
    's3-down': {
        board: 's3', offline: true,
        network: { type: 'wifi', state: 'disconnected', ip: '192.168.1.126', gateway: '192.168.1.1',
            mac: '3C:84:27:C1:2A:F0', dhcp: true, ssid: 'HomeNetwork', rssi: -58 }
    },
    'p4-connected': {
        board: 'p4',
        network: { type: 'ethernet', state: 'connected', ip: '192.168.1.50', gateway: '192.168.1.1',
            mac: '30:ED:A0:E1:4B:7C', dhcp: true, link_speed: 100, full_duplex: true }
    },
    'p4-down': {
        board: 'p4', offline: true,
        network: { type: 'ethernet', state: 'disconnected', ip: '192.168.1.50', gateway: '192.168.1.1',
            mac: '30:ED:A0:E1:4B:7C', dhcp: true, link_speed: null }
    }
};

const NOTES = {
    's3-connected': 'ESP32-S3: <code>/api/config</code> lists only <b>wifi</b>, so only the Wi-Fi section is shown. Status block on top (refreshed every 5 s, like the sidebar: watch the signal), the existing Connection Setup form below, unchanged.',
    's3-ap': 'First setup: the board runs its own network. Badge <b>Access point mode</b> (same info blue as the sidebar "AP Mode Active"), Signal and Gateway do not apply, the notice says the board is not on the home network yet and points to the form. Session row hidden, as today in AP mode.',
    's3-connecting': 'Reported by the API while the board (re)joins the saved network. Fields not known yet are in amber ("Waiting for address"), not blank.',
    's3-down': 'Single-link board: when the link drops the board cannot answer any more, so the page infers it from the failed poll (same moment the sidebar shows "Offline"). Last known values stay, greyed; the badge says <b>Disconnected</b> and the notice says what to check. On a board with two links the API reports <code>state: disconnected</code> directly and the section looks the same.',
    'p4-connected': 'ESP32-P4 (Ethernet only): <code>/api/config</code> lists only <b>ethernet</b>. Same status grid as Wi-Fi: only row 1 changes (Link speed, Cable instead of Network, Signal). No settings form (static IP is out of scope).',
    'p4-down': 'Cable unplugged with the page open: same treatment as Wi-Fi disconnected. Speed and Cable show the link-down values, the rest is the last known address, greyed.',
    'mobile': 'Below 768px: sidebar on top (existing US-042 layout), the grid becomes one row per field (label left, value right), the badge drops under the panel title, the button is full width. The phones are live.'
};

const BADGE = {
    connected: { cls: 'connected', ind: 'success', text: 'Connected' },
    connecting: { cls: 'connecting', ind: 'warning', text: 'Connecting' },
    disconnected: { cls: 'down', ind: 'danger', text: 'Disconnected' },
    ap: { cls: 'ap', ind: 'info', text: 'Access point mode' }
};

const REFRESH_S = 5;
const $ = (id) => document.getElementById(id);

let cur = null;            // current state object (copied)
let age = 0;               // seconds since last status refresh
let clock = null;

// ---------- Helpers ----------
function signalInfo(rssi) {
    if (rssi >= -55) return { level: 4, word: 'Excellent', cls: 'sig-excellent' };
    if (rssi >= -67) return { level: 3, word: 'Good', cls: 'sig-good' };
    if (rssi >= -75) return { level: 2, word: 'Fair', cls: 'sig-fair' };
    return { level: 1, word: 'Weak', cls: 'sig-weak' };
}

function signalHtml(rssi) {
    const s = signalInfo(rssi);
    return `<span class="signal ${s.cls}" data-level="${s.level}" aria-hidden="true"><i></i><i></i><i></i><i></i></span>` +
        `<span>${rssi} dBm</span><span class="signal-word ${s.cls}">${s.word}</span>`;
}

// value: text/html; kind: '' | 'na' (no value / stale) | 'wait' (not known yet)
function setField(id, value, kind = '') {
    const el = $(id);
    el.innerHTML = value;
    el.className = kind;
}

function setBadge(prefix, state) {
    const b = BADGE[state];
    const el = $(prefix + '-badge');
    el.className = 'link-badge ' + b.cls;
    el.querySelector('.status-indicator').className = 'status-indicator ' + b.ind;
    el.querySelector('.badge-text').textContent = b.text;
}

function setNotice(prefix, kind, html) {
    const n = $(prefix + '-notice');
    if (!kind) { n.hidden = true; return; }
    n.hidden = false;
    n.className = 'auth-alert' + (kind === 'danger' ? '' : ' ' + kind);
    // "i" icon for information, "!" for problems
    n.querySelector('path').setAttribute('d', kind === 'info' ? 'M12 16v-4M12 8h.01' : 'M12 8v4M12 16h.01');
    $(prefix + '-notice-text').innerHTML = html;
}

// ---------- Render: Wi-Fi ----------
function renderWifi(n, offline) {
    setBadge('wifi', n.state);
    const dhcp = n.dhcp ? 'On' : 'Off (static)';
    $('wifi-ssid-label').textContent = 'Network (SSID)';

    if (n.state === 'connected') {
        setField('wifi-ssid', n.ssid);
        setField('wifi-signal', signalHtml(n.rssi));
        setField('wifi-ip', n.ip);
        setField('wifi-gw', n.gateway);
        setField('wifi-mac', n.mac);
        setField('wifi-dhcp', dhcp);
        setNotice('wifi', null);
    } else if (n.state === 'ap') {
        $('wifi-ssid-label').textContent = 'Network (SSID) · this board';
        setField('wifi-ssid', n.ssid);
        setField('wifi-signal', 'Not applicable in AP mode', 'na');
        setField('wifi-ip', n.ip);
        setField('wifi-gw', '—', 'na');
        setField('wifi-mac', n.mac);
        setField('wifi-dhcp', 'Server <span class="sub">for joining devices</span>');
        setNotice('wifi', 'info',
            `<span><strong>The board is not on your home network yet.</strong> It runs its own Wi-Fi network ` +
            `<strong>${n.ssid}</strong> and you are connected to it at <strong>${n.ip}</strong>. ` +
            `Enter your home network below and press Initialize Connection.</span>`);
    } else if (n.state === 'connecting') {
        setField('wifi-ssid', n.ssid);
        setField('wifi-signal', 'Waiting…', 'wait');
        setField('wifi-ip', 'Waiting for address…', 'wait');
        setField('wifi-gw', '—', 'na');
        setField('wifi-mac', n.mac);
        setField('wifi-dhcp', dhcp);
        setNotice('wifi', 'warn',
            `<span><strong>Connecting to ${n.ssid}…</strong> This usually takes a few seconds. ` +
            `If it does not connect, check the network name and security key below.</span>`);
    } else {                                  // disconnected: last known values, greyed
        setField('wifi-ssid', n.ssid, 'na');
        setField('wifi-signal', 'No signal', 'na');
        setField('wifi-ip', n.ip, 'na');
        setField('wifi-gw', n.gateway, 'na');
        setField('wifi-mac', n.mac, 'na');
        setField('wifi-dhcp', dhcp, 'na');
        setNotice('wifi', 'danger',
            `<span><strong>Wi-Fi disconnected.</strong> The board is not answering on ${n.ssid}: ` +
            `router off, board out of range or the network changed. It keeps retrying and this page ` +
            `updates by itself when it is back. Values below are the last known ones.</span>`);
    }
}

// ---------- Render: Ethernet ----------
function renderEth(n) {
    setBadge('eth', n.state);
    const dhcp = n.dhcp ? 'On' : 'Off (static)';
    if (n.state === 'connected') {
        setField('eth-speed', `${n.link_speed} Mbps <span class="sub">${n.full_duplex ? 'full duplex' : 'half duplex'}</span>`);
        setField('eth-cable', 'Plugged in');
        setField('eth-ip', n.ip);
        setField('eth-gw', n.gateway);
        setField('eth-mac', n.mac);
        setField('eth-dhcp', dhcp);
        setNotice('eth', null);
    } else {
        setField('eth-speed', 'No link', 'na');
        setField('eth-cable', 'Unplugged', 'na');
        setField('eth-ip', n.ip, 'na');
        setField('eth-gw', n.gateway, 'na');
        setField('eth-mac', n.mac, 'na');
        setField('eth-dhcp', dhcp, 'na');
        setNotice('eth', 'danger',
            `<span><strong>No Ethernet link.</strong> Check that the cable is plugged in at both ends and ` +
            `that the switch or router port is on. This page updates by itself when the link is back. ` +
            `Values below are the last known ones.</span>`);
    }
}

// ---------- Sidebar status bar (same rules as bundle.js fetchSystemStatus) ----------
function renderSidebar(st, board) {
    const n = st.network;
    const label = { connected: 'Connected', connecting: 'Connecting', ap: 'AP Mode Active' }[n.state];
    const ind = { connected: 'success', connecting: 'warning', ap: 'info' }[n.state];
    if (st.offline) {                         // the poll fails: today's catch block
        $('lbl-wifi').textContent = board.net + ': Offline';
        $('ind-wifi').className = 'status-indicator danger';
        $('lbl-ups').textContent = 'UPS: Offline';
        $('ind-ups').className = 'status-indicator danger';
    } else {
        $('lbl-wifi').textContent = `${board.net}: ${label}`;
        $('ind-wifi').className = 'status-indicator ' + ind;
        $('lbl-ups').textContent = 'UPS: Connected';
        $('ind-ups').className = 'status-indicator success';
    }
    $('sidebar-session').hidden = n.state === 'ap';
    document.querySelector('.device-id span').textContent = board.id;
}

// ---------- "No answer for N s" (only while offline) + simulated refresh every REFRESH_S ----------
function liveText() {
    return cur.offline ? `No answer for ${age} s` : '';
}

function tick() {
    age++;
    if (!cur.offline && age >= REFRESH_S) {
        age = 0;
        const n = cur.network;
        if (n.state === 'connected' && n.type === 'wifi') {
            n.rssi = -58 + Math.round((Math.random() - 0.5) * 4);   // -60 .. -56, stays "Good"
            setField('wifi-signal', signalHtml(n.rssi));
        }
    }
    ['wifi-live', 'eth-live'].forEach(id => {
        $(id).textContent = liveText();
        $(id).hidden = !cur.offline;
    });
}

// ---------- State switch ----------
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

    cur = structuredClone(STATES[state]);
    const board = BOARDS[cur.board];

    // Sections from /api/config: shown/hidden by data-link, no per-board code
    document.querySelectorAll('.net-section[data-link]').forEach(sec => {
        sec.hidden = !board.links.includes(sec.dataset.link);
    });

    if (cur.network.type === 'wifi') renderWifi(cur.network);
    else renderEth(cur.network);
    renderSidebar(cur, board);
    resetForm();

    age = cur.offline ? 20 : 2;
    clearInterval(clock);
    tick();
    clock = setInterval(tick, 1000);
}

// ---------- Connection Setup form: same behaviour as today ----------
const btnConnect = $('btn-connect');
const BTN_IDLE = btnConnect.innerHTML;

function resetForm() {
    btnConnect.innerHTML = BTN_IDLE;
    btnConnect.disabled = false;
    btnConnect.removeAttribute('style');
    $('password').disabled = false;
}

$('toggle-pwd').addEventListener('click', () => {
    const p = $('password');
    p.type = p.type === 'password' ? 'text' : 'password';
    $('toggle-pwd').setAttribute('aria-label', p.type === 'password' ? 'Show password' : 'Hide password');
});

$('wifi-form').addEventListener('submit', (e) => {
    e.preventDefault();
    btnConnect.innerHTML = '<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div> Handshake...';
    btnConnect.disabled = true;
    $('password').disabled = true;
    setTimeout(() => {                         // simulated POST /api/wifi/connect
        btnConnect.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg> Saved &amp; Connecting';
        btnConnect.style.cssText = 'background:var(--success);border-color:var(--success);color:#000;box-shadow:0 0 20px rgba(0,255,157,0.4)';
    }, 900);
});

// ---------- Mockup chrome ----------
document.querySelectorAll('.mock-switcher button').forEach(btn =>
    btn.addEventListener('click', () => applyState(btn.dataset.state)));
$('mock-future').addEventListener('change', (e) => { $('mock-ghost').hidden = !e.target.checked; });

// ?embed=1&state=...&scroll=1 is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 's3-connected');
if (params.get('scroll') === '1') {
    window.addEventListener('load', () => document.querySelector('.topbar').scrollIntoView({ block: 'start' }));
}
