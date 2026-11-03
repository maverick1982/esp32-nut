// US-113 mockup: fake data + simulated save/restart. Not production code.
// The Wi-Fi / Ethernet sections are the US-112 page, rendered here in their "connected" state only.

// GET /api/config -> network: { links: [...], hostname: { name, custom, default } }
//   name    = name in use now (custom if set, otherwise default)
//   custom  = name saved in NVS, "" when none
//   default = "esp32-nut-" + last 3 bytes of the MAC, lowercase
const BOARDS = {
    s3: {
        links: ['wifi'], id: '3C842756D784', net: 'Wi-Fi',
        network: { type: 'wifi', ip: '192.168.1.126', gateway: '192.168.1.1', mac: '3C:84:27:56:D7:84', ssid: 'HomeNetwork', rssi: -58 },
        hostDefault: 'esp32-nut-56d784'
    },
    p4: {
        links: ['ethernet'], id: '30EDA0E14B7C', net: 'Ethernet',
        network: { type: 'ethernet', ip: '192.168.1.50', gateway: '192.168.1.1', mac: '30:ED:A0:E1:4B:7C', link_speed: 100 },
        hostDefault: 'esp32-nut-e14b7c'
    }
};

// board + saved custom name + optional UI situation to show
const STATES = {
    's3-default':   { board: 's3', custom: '' },
    'p4-default':   { board: 'p4', custom: '' },
    's3-custom':    { board: 's3', custom: 'espnut-network' },
    'invalid':      { board: 's3', custom: '', typed: 'ups_rack', show: 'invalid' },
    'unchanged':    { board: 's3', custom: 'espnut-network', show: 'unchanged' },
    'restarting':   { board: 's3', custom: '', typed: 'espnut-network', show: 'restarting' },
    'restarting-p4': { board: 'p4', custom: '', typed: 'espnut-network', show: 'restarting' }
};

const NOTES = {
    's3-default': 'ESP32-S3: only the Wi-Fi section (from <code>network.links</code>), then <b>Hostname</b>, which has no <code>data-link</code> and is never hidden. No name chosen: the field is empty, its placeholder and the "In use" line show the default <code>esp32-nut-56d784</code> (last 3 bytes of the MAC). Type a name and press Save Hostname to try the flow.',
    'p4-default': 'ESP32-P4: only the Ethernet section, then the same Hostname section. The name is sent with the Ethernet DHCP request, nothing in the section changes per board.',
    's3-custom': 'A name is saved (<code>network.hostname.custom</code>): the field holds it, the tag says <b>Custom</b>. Emptying the field and saving goes back to the default, which the note under the form names.',
    'invalid': 'Client-side check on Save (regex <code>^[A-Za-z0-9](?:[A-Za-z0-9-]{0,30}[A-Za-z0-9])?$</code>, empty allowed): field in red + inline alert, nothing sent. A 400 <code>{"error"}</code> from <code>POST /api/network/hostname</code> uses the same box with the API text. Typing hides the error.',
    'unchanged': 'Save with the name already in use: the API answers <code>restart:false</code>, the button says <b>No changes</b> for 2 s (frozen here; type something to reset it). The board does not restart.',
    'restarting': '<code>{"success":true,"restart":true}</code>: button <b>Saved</b> (green, as Wi-Fi), field locked, sidebar <b>Restarting...</b>, and the same success alert + countdown as Board Settings > USB port. At 0 the page reloads and shows the new name in use.',
    'mobile': 'Below 768px the form is one column: field, alert, full-width button, as the Wi-Fi form. The "In use" line wraps: the Default/Custom tag drops to its own line, right-aligned. The phones are live and scrolled to the Hostname section.'
};

const HOST_RE = /^[A-Za-z0-9](?:[A-Za-z0-9-]{0,30}[A-Za-z0-9])?$/;
const ERR_INVALID = 'Invalid hostname: use only letters, digits and hyphens, no hyphen at the start or end, at most 32 characters';
const RESTART_S = 15;
const NO_CHANGE_MS = 2000;

const $ = (id) => document.getElementById(id);

let board = null;          // BOARDS entry
let host = null;           // { name, custom, default } as from /api/config
let timers = [];

function clearTimers() { timers.forEach(t => { clearTimeout(t); clearInterval(t); }); timers = []; }

// ---------- Link sections (US-112, connected state only) ----------
function signalHtml(rssi) {
    return `<span class="signal sig-good" data-level="3" aria-hidden="true"><i></i><i></i><i></i><i></i></span>` +
        `<span>${rssi} dBm</span><span class="signal-word sig-good">Good</span>`;
}

function renderLinks() {
    document.querySelectorAll('.net-section[data-link]').forEach(sec => {
        sec.hidden = !board.links.includes(sec.dataset.link);
    });
    const n = board.network;
    if (n.type === 'wifi') {
        $('wifi-ssid').textContent = n.ssid;
        $('wifi-signal').innerHTML = signalHtml(n.rssi);
        $('wifi-ip').textContent = n.ip;
        $('wifi-gw').textContent = n.gateway;
        $('wifi-mac').textContent = n.mac;
        $('wifi-dhcp').textContent = 'On';
        $('wifi-notice').hidden = true;
    } else {
        $('eth-speed').innerHTML = `${n.link_speed} Mbps <span class="sub">full duplex</span>`;
        $('eth-ip').textContent = n.ip;
        $('eth-gw').textContent = n.gateway;
        $('eth-mac').textContent = n.mac;
        $('eth-notice').hidden = true;
    }
    ['wifi-live', 'eth-live'].forEach(id => { $(id).hidden = true; });
    document.querySelector('.device-id span').textContent = board.id;
    setSidebar(false);
}

function setSidebar(restarting) {
    $('lbl-wifi').textContent = `${board.net}: ${restarting ? 'Restarting...' : 'Connected'}`;
    $('ind-wifi').className = 'status-indicator ' + (restarting ? 'warning' : 'success');
}

// ---------- Hostname section ----------
const input = $('hostname');
const btn = $('btn-hostname');
const BTN_IDLE = btn.innerHTML;
const ICON_OK = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>';

function renderHostname() {
    $('hostname-current').textContent = host.name;
    const origin = $('hostname-origin');
    origin.textContent = host.custom ? 'Custom' : 'Default';
    origin.classList.toggle('custom', !!host.custom);
    input.placeholder = host.default;
    input.value = host.custom;
    $('hostname-default').textContent = host.default;
    resetForm();
}

function resetForm() {
    btn.innerHTML = BTN_IDLE;
    btn.disabled = false;
    btn.classList.remove('is-saved');
    input.disabled = false;
    hideError();
    $('hostname-success').hidden = true;
}

function showError(text) {
    $('hostname-error-text').textContent = text;
    $('hostname-error').hidden = false;
    input.classList.add('invalid');
    input.setAttribute('aria-invalid', 'true');
}

function hideError() {
    $('hostname-error').hidden = true;
    input.classList.remove('invalid');
    input.removeAttribute('aria-invalid');
}

function showNoChanges(freeze) {
    btn.innerHTML = 'No changes';
    if (!freeze) timers.push(setTimeout(() => { btn.innerHTML = BTN_IDLE; }, NO_CHANGE_MS));
}

function startRestart(newCustom) {
    const newName = newCustom || host.default;
    btn.innerHTML = ICON_OK + ' Saved';
    btn.classList.add('is-saved');
    btn.disabled = true;
    input.disabled = true;
    $('hostname-new').textContent = newName;
    $('hostname-success').hidden = false;
    setSidebar(true);

    let left = RESTART_S;
    $('hostname-countdown').textContent = left;
    const t = setInterval(() => {
        left -= 1;
        $('hostname-countdown').textContent = Math.max(0, left);
        if (left <= 0) {                           // "reload": /api/config now returns the new name
            clearInterval(t);
            host = { name: newName, custom: newCustom, default: host.default };
            renderHostname();
            setSidebar(false);
        }
    }, 1000);
    timers.push(t);
}

input.addEventListener('input', () => {
    hideError();
    if (!btn.disabled) btn.innerHTML = BTN_IDLE;
});

$('hostname-form').addEventListener('submit', (e) => {
    e.preventDefault();
    if (btn.disabled) return;
    const v = input.value.trim();
    input.value = v;
    if (v && !HOST_RE.test(v)) { showError(ERR_INVALID); input.focus(); return; }

    // simulated POST /api/network/hostname {"hostname": v}
    btn.innerHTML = '<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div> Saving...';
    btn.disabled = true;
    timers.push(setTimeout(() => {
        btn.disabled = false;
        if (v === host.custom) showNoChanges(false);   // {"success":true,"restart":false}
        else startRestart(v);                          // {"success":true,"restart":true}
    }, 600));
});

// ---------- State switch ----------
function applyState(state) {
    document.querySelectorAll('.mock-switcher button').forEach(b =>
        b.classList.toggle('on', b.dataset.state === state));
    $('mock-note').innerHTML = NOTES[state] || NOTES[state.replace('-p4', '')] || '';

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
    clearTimers();

    const st = STATES[state];
    board = BOARDS[st.board];
    host = { name: st.custom || board.hostDefault, custom: st.custom, default: board.hostDefault };
    renderLinks();
    renderHostname();

    if (st.typed !== undefined) input.value = st.typed;
    if (st.show === 'invalid') showError(ERR_INVALID);
    if (st.show === 'unchanged') showNoChanges(true);
    if (st.show === 'restarting') startRestart(st.typed);
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));

// ?embed=1&state=...&scroll=hostname is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 's3-default');
if (params.get('scroll') === 'hostname') {
    // scroll only the content area (on phones the sidebar stays on top of it)
    window.addEventListener('load', () => {
        const area = document.querySelector('.content-area');
        area.scrollTop += $('sec-hostname').getBoundingClientRect().top - area.getBoundingClientRect().top - 16;
    });
}
