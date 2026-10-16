// US-068 mockup — fake data + simulated interactions. Not production code.

// Shape of GET /api/ups/variables (sorted by name):
// { connected, variables: [{ name, description, value, max_length, enum, driver_side, confirm }] }
// Realistic Eaton 3S catalog + two ENUM examples (input.sensitivity, ups.shutdown with confirm).
const CATALOG = [
    { name: 'battery.charge.low', description: 'Remaining battery level when UPS switches to LB (percent)', value: '20', max_length: 5, enum: [], driver_side: false, confirm: false },
    { name: 'input.sensitivity', description: 'Input power sensitivity', value: 'medium', max_length: 0, enum: ['low', 'medium', 'high'], driver_side: false, confirm: false },
    { name: 'input.transfer.high', description: 'High voltage transfer point (V)', value: '264', max_length: 5, enum: [], driver_side: false, confirm: false },
    { name: 'input.transfer.low', description: 'Low voltage transfer point (V)', value: '184', max_length: 5, enum: [], driver_side: false, confirm: false },
    { name: 'outlet.1.desc', description: 'Outlet description', value: 'PowerShare Outlet 1', max_length: 20, enum: [], driver_side: true, confirm: false },
    { name: 'outlet.desc', description: 'Outlet description', value: 'Main Outlet', max_length: 20, enum: [], driver_side: true, confirm: false },
    { name: 'ups.delay.shutdown', description: 'Interval to wait after shutdown with delay command (seconds)', value: '60', max_length: 10, enum: [], driver_side: true, confirm: false },
    { name: 'ups.delay.start', description: 'Interval to wait before (re)starting the load (seconds)', value: '30', max_length: 10, enum: [], driver_side: true, confirm: false },
    { name: 'ups.shutdown', description: 'Enable or disable UPS shutdown ability (poweroff)', value: 'enabled', max_length: 0, enum: ['enabled', 'disabled'], driver_side: false, confirm: true }
];

// Driver defaults used by the fake POST /api/ups/variable/reset
const DRIVER_DEFAULTS = {
    'outlet.1.desc': 'PowerShare Outlet 1',
    'outlet.desc': 'Main Outlet',
    'ups.delay.shutdown': '20',
    'ups.delay.start': '30'
};

// Fallback texts by HTTP status (the server's {"error": "..."} wins when present)
const SET_ERRORS = {
    400: 'Invalid value',
    500: 'Rejected by the UPS',
    503: 'UPS not connected'
};
const SET_CONFIRM_WARNING = 'This setting can switch the UPS outlets, bypass or ECO mode: the devices connected to the UPS may lose power.';
const RESULT_MS = 4000;

const ICON_CHECK = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5"><polyline points="20 6 9 17 4 12"/></svg>';
const ICON_RESET = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="1 4 1 10 7 10"/><path d="M3.51 15a9 9 0 102.13-9.36L1 10"/></svg>';
const ICON_ZAP = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M13 2L3 14h9l-1 8 10-12h-9z"/></svg>';
const ICON_BATTERY = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="7" width="18" height="10" rx="2"/><line x1="22" y1="11" x2="22" y2="13"/><line x1="6" y1="10" x2="6" y2="14"/><line x1="10" y1="10" x2="10" y2="14"/></svg>';
const ICON_CHIP = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="4" y="4" width="16" height="16" rx="2"/><rect x="9" y="9" width="6" height="6"/><path d="M9 1v3M15 1v3M9 20v3M15 20v3M20 9h3M20 14h3M1 9h3M1 14h3"/></svg>';

let vars = [];        // current "server" state

function esc(s) {
    return String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

function nowTime() {
    return new Date().toTimeString().slice(0, 8);
}

function currentHtml(v) {
    return v === '' ? '<span class="set-current-value is-empty">(empty)</span>'
        : `<span class="set-current-value">${esc(v)}</span>`;
}

function fieldHtml(v) {
    const label = `New value for ${esc(v.name)}`;
    if (v.enum.length) {
        return `<label class="set-field enum">
            <select aria-label="${label}">
                ${v.enum.map(o => `<option value="${esc(o)}"${o === v.value ? ' selected' : ''}>${esc(o)}</option>`).join('')}
            </select></label>`;
    }
    return `<label class="set-field">
        <input type="text" value="${esc(v.value)}" maxlength="${v.max_length}" spellcheck="false"
            autocomplete="off" aria-label="${label}">
        <span class="set-counter${v.value.length >= v.max_length ? ' full' : ''}">${v.value.length}/${v.max_length}</span></label>`;
}

function rowHtml(v) {
    const tags = (v.enum.length ? '<span class="set-tag">enum</span>' : '') +
        (v.confirm ? `<span class="set-tag warn" title="Asks for confirmation before applying">${ICON_ZAP}confirm</span>` : '');
    return `
        <div class="cmd-row set-row" data-var="${esc(v.name)}">
            <div class="set-id">
                <div class="cmd-name">${esc(v.name)}</div>
                <div class="set-tags">${tags}</div>
            </div>
            <div class="cmd-desc">${esc(v.description)}</div>
            <div class="set-current">
                <span class="set-current-label">Current</span>
                ${currentHtml(v.value)}
                ${v.driver_side ? `<button type="button" class="btn btn-sm set-reset">
                    ${ICON_RESET} Reset to default</button>` : ''}
            </div>
            <div class="set-edit">
                <div class="set-controls">
                    ${fieldHtml(v)}
                    <button type="button" class="btn btn-outline btn-sm set-apply" disabled>
                        ${ICON_CHECK} Apply</button>
                </div>
                <span class="cmd-result" aria-live="polite"></span>
            </div>
        </div>`;
}

function groupHtml(id, title, sub, icon, list) {
    if (!list.length) return '';
    return `
        <section class="cmd-group set-group" id="${id}">
            <div class="cmd-group-header">${icon}<span>${title}</span>
                <span class="set-group-sub">${sub}</span>
                <span class="cmd-group-count">${list.length}</span></div>
            <div class="set-head"><span>Variable</span><span>Description</span><span>Current</span><span>New value</span></div>
            ${list.map(rowHtml).join('')}
        </section>`;
}

function render(connected) {
    const list = document.getElementById('set-list');
    const empty = document.getElementById('set-empty');
    const isEmpty = !connected || !vars.length;

    document.getElementById('set-snapshot').hidden = isEmpty;
    document.getElementById('set-lead').hidden = isEmpty;
    document.getElementById('set-snapshot-time').textContent = nowTime();

    if (isEmpty) {
        list.innerHTML = '';
        empty.hidden = false;
        return;
    }
    empty.hidden = true;
    // Two groups, each sorted by name as the API returns them
    list.innerHTML =
        groupHtml('set-group-ups', 'UPS parameters', 'stored in the UPS', ICON_BATTERY, vars.filter(v => !v.driver_side)) +
        groupHtml('set-group-driver', 'Driver parameters', 'saved on the ESP32', ICON_CHIP, vars.filter(v => v.driver_side));
}

// ---------- Row helpers ----------
function rowParts(row) {
    const v = vars.find(x => x.name === row.dataset.var);
    return {
        v,
        field: row.querySelector('.set-field'),
        input: row.querySelector('.set-field input, .set-field select'),
        counter: row.querySelector('.set-counter'),
        apply: row.querySelector('.set-apply'),
        reset: row.querySelector('.set-reset'),
        out: row.querySelector('.cmd-result'),
        cur: row.querySelector('.set-current-value')
    };
}

function refreshDirty(row) {
    const p = rowParts(row);
    if (!p.v) return;
    const dirty = p.input.value !== p.v.value;
    p.field.classList.toggle('dirty', dirty);
    if (!p.apply.classList.contains('running')) p.apply.disabled = !dirty;
    if (p.counter) {
        p.counter.textContent = `${p.input.value.length}/${p.v.max_length}`;
        p.counter.classList.toggle('full', p.input.value.length >= p.v.max_length);
    }
}

function setButton(btn, icon, label) {
    btn.innerHTML = icon + ' ' + label;
}

let fadeTimers = new WeakMap();

function showResult(row, ok, html, opts = {}) {
    const p = rowParts(row);
    clearTimeout(fadeTimers.get(p.out));
    p.out.className = 'cmd-result ' + (ok ? 'success' : 'error');
    p.out.innerHTML = (ok ? '&#10003; ' : '&#10005; ') + html;
    p.field.classList.toggle('invalid', !ok);
    // Success fades like the command results; an error stays until the value is edited
    if (ok && !opts.sticky) {
        fadeTimers.set(p.out, setTimeout(() => {
            p.out.classList.add('fade');
            setTimeout(() => { p.out.className = 'cmd-result'; p.out.textContent = ''; }, 600);
        }, RESULT_MS));
    }
}

function setCurrent(row, value, keepGlow) {
    const p = rowParts(row);
    p.v.value = value;
    p.cur.outerHTML = currentHtml(value);
    const cur = row.querySelector('.set-current-value');
    cur.classList.add('updated');
    if (!keepGlow) setTimeout(() => cur.classList.remove('updated'), 2500);
    // the editor follows the re-read value
    p.input.value = value;
    refreshDirty(row);
    document.getElementById('set-snapshot-time').textContent = nowTime();
}

// ---------- Fake API ----------
const NUMERIC = /^(battery\.charge\.low|input\.transfer\.(high|low)|ups\.delay\.(shutdown|start))$/;
const UPS_RANGE = { 'battery.charge.low': [5, 95], 'input.transfer.high': [250, 290], 'input.transfer.low': [160, 200] };

function fakePostVariable(name, value) {
    const v = vars.find(x => x.name === name);
    if (!v) return { status: 400, body: { error: 'Unknown variable' } };
    if (v.enum.length && !v.enum.includes(value)) return { status: 400, body: { error: 'Invalid value' } };
    if (!v.enum.length && value.length > v.max_length) return { status: 400, body: { error: 'Value too long' } };
    if (NUMERIC.test(name) && !/^\d+$/.test(value)) return { status: 400, body: { error: 'Invalid value' } };
    const r = UPS_RANGE[name];
    if (r && (+value < r[0] || +value > r[1])) return { status: 500, body: { error: 'Rejected by the UPS' } };
    return { status: 200, body: { success: true, value } };
}

function errorText(res) {
    return (res.body && res.body.error) || SET_ERRORS[res.status] || `Failed (HTTP ${res.status})`;
}

// ---------- Interactions ----------
const listEl = document.getElementById('set-list');

listEl.addEventListener('input', (e) => {
    const row = e.target.closest('.set-row');
    if (!row) return;
    const p = rowParts(row);
    p.field.classList.remove('invalid');
    if (p.out.classList.contains('error')) { p.out.className = 'cmd-result'; p.out.textContent = ''; }
    refreshDirty(row);
});

listEl.addEventListener('keydown', (e) => {
    if (e.key !== 'Enter') return;
    const row = e.target.closest('.set-row');
    if (row && e.target.matches('input')) { e.preventDefault(); apply(row); }
});

listEl.addEventListener('click', (e) => {
    const row = e.target.closest('.set-row');
    if (!row) return;
    if (e.target.closest('.set-apply')) apply(row);
    if (e.target.closest('.set-reset')) reset(row);
});

function apply(row) {
    const p = rowParts(row);
    if (p.apply.disabled) return;
    const value = p.input.value;
    // Native confirmation, as for the destructive UPS commands; Cancel sends nothing
    if (p.v.confirm && !window.confirm(`Set "${p.v.name}" to "${value}"?\n\n${SET_CONFIRM_WARNING}`)) return;

    const old = p.v.value;
    p.apply.disabled = true;
    p.apply.classList.add('running');
    p.input.disabled = true;
    setButton(p.apply, '<span class="cmd-spinner"></span>', 'Applying…');
    p.out.className = 'cmd-result'; p.out.textContent = '';

    setTimeout(() => {          // simulated POST /api/ups/variable, then GET /api/ups/variables
        const res = fakePostVariable(p.v.name, value);
        p.apply.classList.remove('running');
        p.input.disabled = false;
        setButton(p.apply, ICON_CHECK, 'Apply');
        if (res.status === 200) {
            setCurrent(row, res.body.value);
            showResult(row, true, `Applied <span class="arrow">${esc(old)} &rarr;</span> ${esc(res.body.value)}`);
        } else {
            refreshDirty(row);
            showResult(row, false, esc(errorText(res)));
        }
    }, 900);
}

function reset(row) {
    const p = rowParts(row);
    if (p.reset.disabled) return;
    const old = p.v.value;
    p.reset.disabled = true;
    p.reset.classList.add('running');
    setButton(p.reset, '<span class="cmd-spinner"></span>', 'Resetting…');

    setTimeout(() => {          // simulated POST /api/ups/variable/reset, then GET /api/ups/variables
        const value = DRIVER_DEFAULTS[p.v.name];
        p.reset.disabled = false;
        p.reset.classList.remove('running');
        setButton(p.reset, ICON_RESET, 'Reset to default');
        setCurrent(row, value);
        showResult(row, true, `Reset to default <span class="arrow">${esc(old)} &rarr;</span> ${esc(value)}`);
    }, 900);
}

// ---------- Mockup state switcher ----------
const NOTES = {
    'sta': 'Edit a value: <b>Apply</b> turns on only when it differs from the current one. Try <code>battery.charge.low</code> = 30 (200), <code>abc</code> (400), 99 (500, UPS range), <code>ups.shutdown</code> (native <code>window.confirm</code>), <b>Reset to default</b> on <code>ups.delay.shutdown</code> (60 &rarr; 20).',
    'sta-applied': '<code>battery.charge.low</code> 20 &rarr; 30: the value is re-read (GET), flashes green and the outcome shows <b>old &rarr; new</b>. Success fades after 4 s (kept here for inspection).',
    'sta-error': '400 on <code>battery.charge.low</code> (server <code>{"error":"Invalid value"}</code>) and 500 on <code>input.transfer.high</code>. The red frame and message stay until the value is edited. 503 uses the same slot: <b>UPS not connected</b>.',
    'ap': 'Access Point mode: the page works exactly as in STA (Apply, Reset to default and the <code>window.confirm</code> on <code>ups.shutdown</code>). Only the AP context differs: <b>Wi-Fi: AP Mode Active</b> and no session row in the sidebar.',
    'empty': 'Empty catalog or <code>connected:false</code>: <b>No settings available</b>. No "Read" timestamp.',
    'mobile': 'Below 768px every row stacks: name + tags, description, <b>Current &middot; value &middot; Reset</b> on one line, then field + Apply, then the outcome. No horizontal scroll at 375px. The phones are live: interact inside them (left: after Apply, right: apply error).'
};

function fresh() {
    return CATALOG.map(v => ({ ...v, enum: [...v.enum] }));
}

function setChrome(state) {
    const ap = state === 'ap';
    const disc = state === 'empty';
    document.getElementById('sidebar-session').hidden = ap;
    document.getElementById('ind-wifi').className = 'status-indicator ' + (ap ? 'info' : 'success');
    document.getElementById('lbl-wifi').textContent = ap ? 'Wi-Fi: AP Mode Active' : 'Wi-Fi: Connected';
    document.getElementById('ind-ups').className = 'status-indicator ' + (disc ? 'danger' : 'success');
    document.getElementById('lbl-ups').textContent = disc ? 'UPS: Disconnected' : 'UPS: Connected';
}

function applyState(state) {
    document.querySelectorAll('.mock-switcher button').forEach(b =>
        b.classList.toggle('on', b.dataset.state === state));
    document.getElementById('mock-note').innerHTML = NOTES[state] || '';

    const stage = document.getElementById('phone-stage');
    const app = document.getElementById('app');
    if (state === 'mobile') {
        stage.hidden = false;
        app.hidden = true;
        stage.querySelectorAll('iframe').forEach(f => { if (!f.src) f.src = f.dataset.src; });
        return;
    }
    stage.hidden = true;
    app.hidden = false;

    vars = fresh();
    setChrome(state);
    render(state !== 'empty');

    if (state === 'sta-applied') {
        const row = listEl.querySelector('[data-var="battery.charge.low"]');
        setCurrent(row, '30', true);        // keep the glow for inspection
        showResult(row, true, 'Applied <span class="arrow">20 &rarr;</span> 30', { sticky: true });
    }
    if (state === 'sta-error') {
        let row = listEl.querySelector('[data-var="battery.charge.low"]');
        rowParts(row).input.value = '3O';
        refreshDirty(row);
        showResult(row, false, 'Invalid value');
        row = listEl.querySelector('[data-var="input.transfer.high"]');
        rowParts(row).input.value = '320';
        refreshDirty(row);
        showResult(row, false, 'Rejected by the UPS');
    }
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));

// ?embed=1&state=... is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 'sta');
