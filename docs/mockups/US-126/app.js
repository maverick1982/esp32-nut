// US-126 mockup: fake data + simulated save/restart. Not production code.
// The Wi-Fi / Ethernet / Hostname sections are the v1.14.0 page; only the W5500 section is new.

const $ = (id) => document.getElementById(id);

// ---------- Fake board data ----------
const WIFI = { ip: '192.168.1.126', gateway: '192.168.1.1', mac: '3C:84:27:56:D7:84', ssid: 'HomeNetwork', rssi: -58 };
const ETH  = { ip: '192.168.1.51',  gateway: '192.168.1.1', mac: '3C:84:27:56:D7:87', link_speed: 100 };
const EXAMPLE = { sck: 12, mosi: 11, miso: 13, cs: 10, irq: 9, rst: 14 };  // GPIO 9-14: adjacent header pins, no strapping pin
const MICKEL  = { sck: 12, mosi: 11, miso: 13, cs: 10, irq: 9, rst: 3 };   // issue #94 wiring (RST on strapping GPIO 3)
const EMPTY = { sck: null, mosi: null, miso: null, cs: null, irq: null, rst: null };
const PIN_FIELDS = ['sck', 'mosi', 'miso', 'cs', 'irq', 'rst'];
const PIN_LABEL = { sck: 'SCK', mosi: 'MOSI', miso: 'MISO', cs: 'CS', irq: 'INT', rst: 'RST' };
const REQUIRED = ['sck', 'mosi', 'miso', 'cs'];

// Same rules as checkEthSpiSettings() of the plan (profile of the ESP32-S3 DevKit)
const GPIO_MAX = 48;
const RESERVED = [
    [19, 20, 'USB port of the UPS'],
    [22, 25, 'not present on the ESP32-S3'],
    [26, 32, 'flash and PSRAM'],
    [33, 37, 'octal PSRAM'],
    [43, 44, 'serial console'],
    [48, 48, 'status LED'],
    [38, 38, 'status LED']
];
const STRAPPING = [0, 3, 45, 46];

const FALLBACK_TEXT = {
    no_chip: '<strong>The W5500 did not answer at startup.</strong> Check the wiring and the pins below. The board is using Wi-Fi.',
    no_link: '<strong>No Ethernet cable link at startup.</strong> Check the cable and the switch port. The board is using Wi-Fi.',
    no_ip:   '<strong>The router gave no address over Ethernet (DHCP).</strong> The board is using Wi-Fi.'
};

// saved = what GET /api/network/eth returns; typed = what the user has in the fields; show = UI situation
const STATES = {
    off:        { enabled: false, pins: EMPTY,   state: 'off' },
    active:     { enabled: true,  pins: EXAMPLE, state: 'active' },
    no_chip:    { enabled: true,  pins: { sck: 12, mosi: 11, miso: 13, cs: 8, irq: 9, rst: 14 }, state: 'fallback', reason: 'no_chip' },
    no_link:    { enabled: true,  pins: EXAMPLE, state: 'fallback', reason: 'no_link' },
    no_ip:      { enabled: true,  pins: EXAMPLE, state: 'fallback', reason: 'no_ip' },
    forbidden:  { enabled: false, pins: EMPTY, state: 'off', typedOn: true, typed: { sck: 19, mosi: 11, miso: 13, cs: 10, irq: null, rst: null }, show: 'submit' },
    missing:    { enabled: false, pins: EMPTY, state: 'off', typedOn: true, typed: { sck: 12, mosi: 11, miso: null, cs: 10, irq: null, rst: null }, show: 'submit' },
    strapping:  { enabled: false, pins: EMPTY, state: 'off', typedOn: true, typed: MICKEL },
    unchanged:  { enabled: true,  pins: EXAMPLE, state: 'active', show: 'unchanged' },
    restarting: { enabled: false, pins: EMPTY, state: 'off', typedOn: true, typed: EXAMPLE, show: 'restart' },
    p4:         { p4: true }
};

const NOTES = {
    off: 'ESP32-S3, module off (default): Wi-Fi section only, then the new <b>Ethernet module (W5500)</b> section, then Hostname. The six fields are empty; the grey numbers are only an example wiring (placeholder): GPIO 9-14, six adjacent pins on the DevKitC-1 header, none of them a strapping pin. Turn the switch on, type the pins and press Save to try the flow.',
    active: 'Module on and answering: <code>network.links</code> now has <code>ethernet</code>, so the Ethernet section of US-112 appears and is the active link (sidebar <b>Ethernet: Connected</b>); the Wi-Fi section shows "Not the active link". Badge of the W5500 section: <b>Active</b>.',
    no_chip: 'Fallback, <code>reason: no_chip</code>: <code>ETH.begin()</code> failed (here CS on the wrong GPIO 8). Amber note on top of the form, badge <b>Fallback</b>, the board runs on Wi-Fi. The Ethernet section stays visible as an inactive link.',
    no_link: 'Fallback, <code>reason: no_link</code>: the W5500 answered but no cable link came within 5 s.',
    no_ip: 'Fallback, <code>reason: no_ip</code>: link up but no DHCP address within 15 s of the link.',
    forbidden: 'Save with SCK on GPIO 19: <code>POST /api/network/eth</code> answers 400 <code>{"error":"GPIO 19 cannot be used for SCK: USB port of the UPS"}</code>. The named field turns red; typing hides the error. Same box for every rule: not present, flash/PSRAM, octal PSRAM, serial console, status LED, duplicates.',
    missing: 'Switch on with MISO empty: 400 <code>"MISO is required to enable the W5500"</code>. INT and RST can stay empty (polling, no hardware reset).',
    strapping: 'The issue #94 wiring, with RST on GPIO 3, a strapping pin: amber note while typing, field outlined in amber, saving stays allowed (the issue #94 wiring uses it). The POST answer repeats it in <code>warnings</code>.',
    unchanged: 'Save without changes: <code>restart:false</code>, the button says <b>No changes</b> for 2 s (frozen here; edit a field to reset). No restart.',
    restarting: '<code>{"success":true,"restart":true}</code>: button <b>Saved</b> (green), fields locked, sidebar <b>Restarting...</b>, countdown as the Hostname section. At 0 the page "reloads" in the Active state.',
    p4: 'ESP32-P4: <code>GET /api/network/eth</code> answers 404, the W5500 section is not rendered. The page is the v1.14.0 one: Ethernet (RMII) and Hostname.',
    mobile: 'Below 768px the pin grid has 2 columns (SCK MOSI / MISO CS / INT RST), the role labels are dropped except "optional", the button is full width. The phones are live and scrolled to the W5500 section.'
};

const RESTART_S = 15;
const NO_CHANGE_MS = 2000;
let timers = [];
let saved = null;          // { enabled, pins, state, reason }
let p4 = false;

function clearTimers() { timers.forEach(t => { clearTimeout(t); clearInterval(t); }); timers = []; }

// ---------- Badges ----------
function badge(el, cls, text, ind) {
    el.className = 'link-badge ' + cls + (el.id === 'w5500-badge' ? ' w5500-badge' : '');
    el.querySelector('.status-indicator').className = 'status-indicator ' + ind;
    el.querySelector('.badge-text').textContent = text;
}

function notice(prefix, html, info) {
    const box = $(prefix + '-notice');
    if (!html) { box.hidden = true; return; }
    box.className = 'auth-alert' + (info ? ' info' : '');
    $(prefix + '-notice-text').innerHTML = html;
    box.hidden = false;
}

function setSidebar(restarting) {
    const onEth = p4 || (saved && saved.state === 'active');
    $('lbl-wifi').textContent = `${onEth ? 'Ethernet' : 'Wi-Fi'}: ${restarting ? 'Restarting...' : 'Connected'}`;
    $('ind-wifi').className = 'status-indicator ' + (restarting ? 'warning' : 'success');
}

// ---------- Link sections (v1.14.0) ----------
function renderLinks() {
    const ethShown = p4 || saved.enabled;
    const wifiActive = !p4 && saved.state !== 'active';
    $('sec-wifi').hidden = p4;
    $('sec-eth').hidden = !ethShown;

    // Wi-Fi
    $('wifi-ssid').textContent = WIFI.ssid;
    $('wifi-signal').innerHTML = `<span class="signal sig-good" data-level="3" aria-hidden="true"><i></i><i></i><i></i><i></i></span>` +
        `<span>${WIFI.rssi} dBm</span><span class="signal-word sig-good">Good</span>`;
    $('wifi-ip').textContent = wifiActive ? WIFI.ip : '—';
    $('wifi-gw').textContent = wifiActive ? WIFI.gateway : '—';
    $('wifi-mac').textContent = WIFI.mac;
    $('wifi-dhcp').textContent = wifiActive ? 'On' : '—';
    if (wifiActive) { badge($('wifi-badge'), 'connected', 'Connected', 'success'); notice('wifi', null); }
    else {
        badge($('wifi-badge'), 'idle', 'Inactive', 'off');
        $('wifi-signal').textContent = '—';
        notice('wifi', '<b>Not the active link.</b> The board is connected through another link.', true);
    }

    // Ethernet
    const ethActive = p4 || saved.state === 'active';
    if (ethActive) {
        badge($('eth-badge'), 'connected', 'Connected', 'success');
        $('eth-speed').innerHTML = `${ETH.link_speed} Mbps <span class="sub">full duplex</span>`;
        $('eth-cable').textContent = 'Plugged in';
        $('eth-ip').textContent = p4 ? '192.168.1.50' : ETH.ip;
        $('eth-gw').textContent = ETH.gateway;
        $('eth-mac').textContent = p4 ? '30:ED:A0:E1:4B:7C' : ETH.mac;
        $('eth-dhcp').textContent = 'On';
        notice('eth', null);
    } else {
        badge($('eth-badge'), 'idle', 'Inactive', 'off');
        ['eth-speed', 'eth-cable', 'eth-ip', 'eth-gw', 'eth-dhcp'].forEach(id => { $(id).textContent = '—'; });
        $('eth-mac').textContent = ETH.mac;
        notice('eth', '<b>Not the active link.</b> The board is connected through another link.', true);
    }
    ['wifi-live', 'eth-live'].forEach(id => { $(id).hidden = true; });

    document.querySelector('.device-id span').textContent = p4 ? '30EDA0E14B7C' : '3C842756D784';
    const host = p4 ? 'esp32-nut-e14b7c' : 'esp32-nut-56d784';
    $('hostname-current').textContent = host;
    $('hostname').placeholder = host;
    $('hostname-default').textContent = host;
    setSidebar(false);
}

// ---------- W5500 section ----------
const form = $('w5500-form');
const toggle = $('w5500-enabled');
const btn = $('btn-w5500');
const BTN_IDLE = btn.innerHTML;
const ICON_OK = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 6L9 17l-5-5"/></svg>';
const input = (f) => $('w5500-' + f);
const cell = (f) => input(f).closest('.pin-cell');

function renderBadge() {
    const b = $('w5500-badge');
    if (saved.state === 'active') badge(b, 'connected', 'Active', 'success');
    else if (saved.state === 'fallback') badge(b, 'connecting', 'Fallback', 'warning');
    else badge(b, 'idle', 'Off', 'off');

    const st = $('w5500-status');
    if (saved.state === 'fallback') {
        $('w5500-status-text').innerHTML = FALLBACK_TEXT[saved.reason];
        st.hidden = false;
    } else st.hidden = true;
}

function setToggle(on) {
    toggle.checked = on;
    $('w5500-state').textContent = on ? 'On' : 'Off';
    $('w5500-state').classList.toggle('on', on);
    form.classList.toggle('is-off', !on);
}

function fillPins(pins) {
    PIN_FIELDS.forEach(f => { input(f).value = pins[f] === null || pins[f] === undefined ? '' : pins[f]; });
    refreshPins();
}

function readPins() {
    const out = {};
    PIN_FIELDS.forEach(f => {
        const v = input(f).value.trim();
        out[f] = v === '' ? null : Number(v);
    });
    return out;
}

// Wire glow + strapping note, live while typing
function refreshPins() {
    const pins = readPins();
    const straps = [];
    PIN_FIELDS.forEach(f => {
        cell(f).classList.toggle('is-set', pins[f] !== null);
        const isStrap = pins[f] !== null && STRAPPING.includes(pins[f]);
        cell(f).classList.toggle('strap', isStrap);
        if (isStrap) straps.push(`GPIO ${pins[f]} (${PIN_LABEL[f]})`);
    });
    if (straps.length) {
        $('w5500-strapping-text').innerHTML = `<strong>${straps.join(', ')} ${straps.length > 1 ? 'are strapping pins' : 'is a strapping pin'}.</strong> ` +
            'The board may not start if the module pulls it high or low at power-on. It works with most modules; you can still save.';
        $('w5500-strapping').hidden = false;
    } else $('w5500-strapping').hidden = true;
}

// Simulated server check (WebApiJson::checkEthSpiBody -> checkEthSpiSettings)
function serverCheck(enabled, pins) {
    const used = {};
    for (const f of PIN_FIELDS) {
        const g = pins[f];
        if (g === null) continue;
        if (!Number.isInteger(g) || g < 0 || g > GPIO_MAX) return { field: f, error: `GPIO ${g} does not exist on this board` };
        const r = RESERVED.find(([a, b]) => g >= a && g <= b);
        if (r) return { field: f, error: `GPIO ${g} cannot be used for ${PIN_LABEL[f]}: ${r[2]}` };
        if (used[g]) return { field: f, error: `GPIO ${g} is used for both ${PIN_LABEL[used[g]]} and ${PIN_LABEL[f]}` };
        used[g] = f;
    }
    if (enabled) {
        for (const f of REQUIRED) if (pins[f] === null) return { field: f, error: `${PIN_LABEL[f]} is required to enable the W5500` };
    }
    return null;
}

function showError(field, text) {
    $('w5500-error-text').textContent = text;
    $('w5500-error').hidden = false;
    if (field) { cell(field).classList.add('invalid'); input(field).setAttribute('aria-invalid', 'true'); }
}

function hideError() {
    $('w5500-error').hidden = true;
    PIN_FIELDS.forEach(f => { cell(f).classList.remove('invalid'); input(f).removeAttribute('aria-invalid'); });
}

function resetForm() {
    btn.innerHTML = BTN_IDLE;
    btn.disabled = false;
    btn.classList.remove('is-saved');
    toggle.disabled = false;
    PIN_FIELDS.forEach(f => { input(f).disabled = false; });
    hideError();
    $('w5500-success').hidden = true;
}

function renderW5500() {
    $('sec-w5500').hidden = p4;
    if (p4) return;
    renderBadge();
    setToggle(saved.enabled);
    fillPins(saved.pins);
    resetForm();
}

function samePins(a, b) { return PIN_FIELDS.every(f => a[f] === b[f]); }

function showNoChanges(freeze) {
    btn.innerHTML = 'No changes';
    if (!freeze) timers.push(setTimeout(() => { btn.innerHTML = BTN_IDLE; }, NO_CHANGE_MS));
}

function startRestart(next) {
    btn.innerHTML = ICON_OK + ' Saved';
    btn.classList.add('is-saved');
    btn.disabled = true;
    toggle.disabled = true;
    PIN_FIELDS.forEach(f => { input(f).disabled = true; });
    $('w5500-success').hidden = false;
    setSidebar(true);

    let left = RESTART_S;
    $('w5500-countdown').textContent = left;
    const t = setInterval(() => {
        left -= 1;
        $('w5500-countdown').textContent = Math.max(0, left);
        if (left <= 0) {                     // "reload": the module answered, Ethernet is the active link
            clearInterval(t);
            saved = { enabled: next.enabled, pins: next.pins, state: next.enabled ? 'active' : 'off' };
            renderLinks();
            renderW5500();
        }
    }, 1000);
    timers.push(t);
}

function submit() {
    if (btn.disabled) return;
    hideError();
    const next = { enabled: toggle.checked, pins: readPins() };
    btn.innerHTML = '<div class="spinner" style="width:14px;height:14px;border-width:1px;"></div> Saving...';
    btn.disabled = true;
    timers.push(setTimeout(() => {           // simulated POST /api/network/eth
        btn.disabled = false;
        btn.innerHTML = BTN_IDLE;
        const err = serverCheck(next.enabled, next.pins);
        if (err) { showError(err.field, err.error); input(err.field).focus(); return; }
        if (next.enabled === saved.enabled && samePins(next.pins, saved.pins)) { showNoChanges(false); return; }
        startRestart(next);
    }, 600));
}

PIN_FIELDS.forEach(f => input(f).addEventListener('input', () => {
    hideError();
    refreshPins();
    if (!btn.disabled) btn.innerHTML = BTN_IDLE;
}));
toggle.addEventListener('change', () => { setToggle(toggle.checked); hideError(); if (!btn.disabled) btn.innerHTML = BTN_IDLE; });
form.addEventListener('submit', (e) => { e.preventDefault(); submit(); });

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
    clearTimers();

    const st = STATES[state];
    p4 = !!st.p4;
    saved = p4 ? null : { enabled: st.enabled, pins: st.pins, state: st.state, reason: st.reason };
    renderLinks();
    renderW5500();
    if (p4) return;

    if (st.typedOn) setToggle(true);
    if (st.typed) fillPins(st.typed);
    if (st.show === 'submit') {
        const err = serverCheck(true, readPins());
        if (err) showError(err.field, err.error);
    }
    if (st.show === 'unchanged') showNoChanges(true);
    if (st.show === 'restart') startRestart({ enabled: true, pins: readPins() });
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));

// ?embed=1&state=...&scroll=w5500 is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 'off');
if (params.get('scroll') === 'w5500') {
    window.addEventListener('load', () => {
        const area = document.querySelector('.content-area');
        area.scrollTop += $('sec-w5500').getBoundingClientRect().top - area.getBoundingClientRect().top - 16;
    });
}
