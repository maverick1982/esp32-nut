// US-115 mockup: fake data, client-side validation and simulated save. Not production code.
//
// Assumed API (to be confirmed by the plan):
//   GET    /api/nut/custom-vars  -> { max: 16, vars: [{ key, value, description, ups_value|null }] }
//          ups_value = what the UPS reports for that key right now, null when it does not report it
//   POST   /api/nut/custom-vars  { key, value, description, old_key? } -> 200 | 400 { error }
//   DELETE /api/nut/custom-vars  { key }
//   GET /api/ups/variables: each writable variable gets  override: "<custom value>" | null

const MAX = 16;
const MAX_LEN = 64;

// What the UPS (Eaton 3S 700) reports today, used for badges and the key hint
const UPS = {
    'ups.model': 'Eaton 3S 700',
    'ups.mfr': 'EATON',
    'ups.serial': '000000000000',
    'battery.charge': '100',
    'battery.charge.low': '20',
    'battery.runtime': '2245',
    'battery.runtime.low': '120',
    'battery.type': 'PbAc',
    'input.voltage': '231.0',
    'ups.beeper.status': 'enabled',
    'ups.delay.shutdown': '20'
};
// Writable variables (UPS Settings tab): an override makes them read-only there
const WRITABLE = ['battery.charge.low', 'battery.runtime.low', 'ups.beeper.status', 'ups.delay.shutdown'];

const BASE = [
    { key: 'battery.date', value: '2024/03/15', description: 'Battery replaced March 2024' },
    { key: 'ups.model', value: 'Eaton 3S 700 DIN', description: 'Full model name for Home Assistant' },
    { key: 'battery.charge.low', value: '30', description: '' }
];
const EXTRA = [
    { key: 'device.location', value: 'Rack 2, office', description: '' },
    { key: 'device.contact', value: 'it@home.lan', description: 'Who to call' },
    { key: 'device.description', value: 'Office rack UPS', description: '' },
    { key: 'ups.id', value: 'office-ups', description: '' },
    { key: 'ups.serial', value: 'G116E04087', description: 'From the label, the UPS reports zeros' },
    { key: 'battery.type', value: 'VRLA', description: '' },
    { key: 'battery.mfr.date', value: '2024/01/10', description: 'Printed on the battery' },
    { key: 'battery.packs', value: '1', description: '' },
    { key: 'battery.voltage.nominal', value: '12', description: '' },
    { key: 'battery.charge.warning', value: '50', description: '' },
    { key: 'input.voltage.nominal', value: '230', description: '' },
    { key: 'ups.power.nominal', value: '700', description: 'VA, from the datasheet' },
    { key: 'ups.realpower.nominal', value: '420', description: 'W, from the datasheet' }
];

const STATES = {
    list:     { vars: BASE },
    empty:    { vars: [] },
    edit:     { vars: BASE, edit: 'ups.model' },
    error:    { vars: BASE, typed: { key: 'ups.status', value: 'OL', description: 'Always on line' }, submit: true },
    limit:    { vars: BASE.concat(EXTRA) },
    settings: { vars: BASE, tab: 'settings' }
};

const NOTES = {
    list: 'Second panel of the <b>NUT Server</b> tab, under the credentials. Each record says what NUT clients get: <b>Added</b> (the UPS does not report the key) or <b>Overrides UPS</b> with the real value struck through under it. Empty description = dash. Try the form: type <code>ups.model</code> or <code>battery.charge.low</code> in Key to see the hint, add a variable, edit, delete.',
    empty: 'No record: <code>.cmd-empty</code> box, counter <b>0 / 16</b>, form ready to add the first one.',
    edit: 'Pencil on <code>ups.model</code>: the form fills in, gets a solid cyan border and the title <b>Edit</b>; the row is marked, the others dimmed. The key stays editable (renaming = POST with <code>old_key</code>). Button <b>Save changes</b> + <b>Cancel</b>. Esc also cancels.',
    error: 'Add with key <code>ups.status</code>: nothing is sent, the key field turns red and the alert under the form gives the reason. Same box for the API 400 <code>{"error"}</code>. Typing in the field hides the error. Other rejected cases to try: <code>driver.parameter.port</code>, <code>device.type</code>, <code>Battery Date</code>, a key already in the list.',
    limit: '16 / 16: counter in amber, fields and <b>Add variable</b> disabled, info alert. Edit stays available (it does not add a record); deleting one re-enables the form.',
    settings: '<b>UPS Settings</b> tab: <code>battery.charge.low</code> has a custom value, so its row is read-only. Tag <b>override</b>, Current = real UPS value (20), input and Apply disabled, note with the value NUT clients see and a link to the NUT Server tab, which opens with that record highlighted.',
    mobile: 'Below 768px a record is: key + badge with Edit/Delete on the right, then value (UPS value under it), then description. The form is one column; the key hint sits under Key, alerts above the full-width buttons. Phones are live.'
};

// NUT names: lowercase, dot-separated words (letters, digits, "_" and "-")
const KEY_RE = /^[a-z0-9][a-z0-9_-]*(?:\.[a-z0-9_-]+)*$/;

const $ = (id) => document.getElementById(id);
const esc = (s) => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const ICON_EDIT = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 20h9"/><path d="M16.5 3.5a2.121 2.121 0 013 3L7 19l-4 1 1-4L16.5 3.5z"/></svg>';
const ICON_DEL = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6M10 11v6M14 11v6M9 6V4a1 1 0 011-1h4a1 1 0 011 1v2"/></svg>';
const ICON_PLUS = '<path d="M12 5v14M5 12h14" />';
const ICON_PEN = '<path d="M12 20h9"/><path d="M16.5 3.5a2.121 2.121 0 013 3L7 19l-4 1 1-4L16.5 3.5z"/>';
const ICON_LAYERS = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/></svg>';

let vars = [];
let editing = null;      // key of the record in the form, null = add mode
let okTimer = null;

const form = $('cv-form');
const inKey = $('cv-key');
const inVal = $('cv-value');
const inDesc = $('cv-desc');

// ---------- List ----------
function badge(v) {
    const ups = UPS[v.key];
    return ups === undefined
        ? '<span class="set-tag cv-tag add" title="The UPS does not report this variable">Added</span>'
        : `<span class="set-tag cv-tag ovr" title="Replaces the value reported by the UPS">${ICON_LAYERS}Overrides UPS</span>`;
}

function renderList(flashKey) {
    const list = $('cv-list');
    list.innerHTML = vars.map(v => {
        const ups = UPS[v.key];
        const upsLine = ups === undefined ? '' :
            `<span class="cv-ups">UPS: <s>${esc(ups)}</s>${WRITABLE.includes(v.key) ? ' &middot; <em>read-only in UPS Settings</em>' : ''}</span>`;
        const cls = ['cmd-row', 'cv-row'];
        if (v.key === editing) cls.push('editing');
        if (v.key === flashKey) cls.push('saved');
        return `<div class="${cls.join(' ')}" data-key="${esc(v.key)}">
            <div class="cv-id"><div class="cmd-name">${esc(v.key)}</div><div class="set-tags">${badge(v)}</div></div>
            <div class="cv-val"><span class="set-current-value">${esc(v.value)}</span>${upsLine}</div>
            <div class="cmd-desc">${esc(v.description)}</div>
            <div class="cv-acts">
                <button type="button" class="cv-act edit" title="Edit ${esc(v.key)}" aria-label="Edit ${esc(v.key)}">${ICON_EDIT}</button>
                <button type="button" class="cv-act del" title="Delete ${esc(v.key)}" aria-label="Delete ${esc(v.key)}">${ICON_DEL}</button>
            </div>
        </div>`;
    }).join('');

    $('cv-group').hidden = vars.length === 0;
    $('cv-group').classList.toggle('is-editing', editing !== null);
    $('cv-empty').hidden = vars.length > 0;
    $('cv-count-n').textContent = vars.length;
    $('cv-count').classList.toggle('full', vars.length >= MAX);

    list.querySelectorAll('.cv-row').forEach(row => {
        const key = row.dataset.key;
        row.querySelector('.edit').addEventListener('click', () => startEdit(key));
        row.querySelector('.del').addEventListener('click', () => removeVar(key));
    });
    refreshForm();
    syncSettings();
}

// ---------- Form ----------
function counter(input, el) {
    const n = input.value.length;
    el.textContent = `${n}/${MAX_LEN}`;
    el.classList.toggle('full', n >= MAX_LEN);
}

function refreshForm() {
    const full = vars.length >= MAX && editing === null;
    form.classList.toggle('is-edit', editing !== null);
    form.classList.toggle('is-full', full);
    [inKey, inVal, inDesc].forEach(i => { i.disabled = full; });
    $('cv-submit').disabled = full;
    $('cv-limit').hidden = !full;
    $('cv-cancel').hidden = editing === null;
    $('cv-submit-text').textContent = editing === null ? 'Add variable' : 'Save changes';
    const title = $('cv-form-title');
    title.querySelector('svg').innerHTML = editing === null ? ICON_PLUS : ICON_PEN;
    $('cv-form-title-text').innerHTML = editing === null ? 'Add a variable' : `Edit <code>${esc(editing)}</code>`;
    counter(inVal, $('cv-value-count'));
    counter(inDesc, $('cv-desc-count'));
    updateHint();
}

function updateHint() {
    const key = inKey.value.trim();
    const hint = $('cv-hint');
    if (!key || !KEY_RE.test(key) || reservedReason(key)) { hint.innerHTML = ''; return; }
    const ups = UPS[key];
    let html = ups === undefined
        ? '<span class="set-tag cv-tag add">Added</span><span>The UPS does not report this variable: NUT clients will see it as an extra one.</span>'
        : `<span class="set-tag cv-tag ovr">${ICON_LAYERS}Overrides UPS</span><span>The UPS reports <b>${esc(ups)}</b>: NUT clients will see your value instead. The dashboard keeps showing ${esc(ups)}.`;
    if (ups !== undefined && WRITABLE.includes(key)) html += ' While this exists, the variable is read-only in UPS Settings.';
    if (ups !== undefined) html += '</span>';
    hint.innerHTML = html;
}

function reservedReason(key) {
    if (key === 'ups.status' || key.startsWith('ups.status.')) return `<code>${esc(key)}</code> cannot be overridden: NUT clients always get the status computed by the board`;
    if (key === 'device.type') return '<code>device.type</code> cannot be overridden';
    if (key.startsWith('driver.parameter.')) return '<code>driver.parameter.*</code> variables cannot be overridden: they describe the board driver';
    return null;
}

function validate(key, value, description) {
    if (!key) return { field: inKey, msg: 'Enter a key, for example <code>battery.date</code>' };
    if (!KEY_RE.test(key)) return { field: inKey, msg: `Invalid key <code>${esc(key)}</code>: use a lowercase NUT name such as <code>battery.date</code> (letters, digits, dots, _ and -)` };
    const r = reservedReason(key);
    if (r) return { field: inKey, msg: r };
    if (key !== editing && vars.some(v => v.key === key)) return { field: inKey, msg: `<code>${esc(key)}</code> already exists: edit it in the list above` };
    if (!value) return { field: inVal, msg: 'Enter a value' };
    if (value.length > MAX_LEN) return { field: inVal, msg: `Value too long: at most ${MAX_LEN} characters` };
    if (description.length > MAX_LEN) return { field: inDesc, msg: `Description too long: at most ${MAX_LEN} characters` };
    if (editing === null && vars.length >= MAX) return { field: null, msg: `Limit reached: at most ${MAX} custom variables` };
    return null;
}

function showError(err) {
    hideOk();
    [inKey, inVal, inDesc].forEach(i => i.classList.remove('invalid'));
    if (err.field) err.field.classList.add('invalid');
    $('cv-error-text').innerHTML = err.msg;
    $('cv-error').hidden = false;
}
function hideError() {
    [inKey, inVal, inDesc].forEach(i => i.classList.remove('invalid'));
    $('cv-error').hidden = true;
}
function showOk(html) {
    clearTimeout(okTimer);
    $('cv-ok-text').innerHTML = html;
    $('cv-ok').hidden = false;
    okTimer = setTimeout(hideOk, 4000);
}
function hideOk() { clearTimeout(okTimer); $('cv-ok').hidden = true; }

function clearForm() {
    inKey.value = ''; inVal.value = ''; inDesc.value = '';
}

function startEdit(key) {
    const v = vars.find(x => x.key === key);
    if (!v) return;
    editing = key;
    inKey.value = v.key; inVal.value = v.value; inDesc.value = v.description;
    hideError(); hideOk();
    renderList();
    inVal.focus();
    inVal.select();
}

function cancelEdit() {
    editing = null;
    clearForm(); hideError();
    renderList();
}

function removeVar(key) {
    const ups = UPS[key];
    const after = ups === undefined ? 'NUT clients will no longer see it.' : `NUT clients will see the UPS value again (${ups}).`;
    if (!window.confirm(`Delete the custom variable "${key}"?\n\n${after}`)) return;
    vars = vars.filter(v => v.key !== key);
    if (editing === key) { editing = null; clearForm(); }
    hideError();
    renderList();
    showOk(`<code>${esc(key)}</code> deleted. ${esc(after)}`);
}

function submit() {
    const key = inKey.value.trim().toLowerCase();
    const value = inVal.value.trim();
    const description = inDesc.value.trim();
    const err = validate(key, value, description);
    if (err) { showError(err); (err.field || inKey).focus(); return; }
    hideError();
    const btn = $('cv-submit');
    btn.disabled = true;
    $('cv-submit-text').textContent = 'Saving…';
    setTimeout(() => {
        const rec = { key, value, description };
        let msg;
        if (editing !== null) {
            vars = vars.map(v => v.key === editing ? rec : v);
            msg = `<code>${esc(key)}</code> saved. NUT clients see <strong>${esc(value)}</strong> now.`;
        } else {
            vars = vars.concat(rec);
            msg = `<code>${esc(key)}</code> added. NUT clients see <strong>${esc(value)}</strong> now, no restart needed.`;
        }
        editing = null;
        clearForm();
        renderList(key);
        showOk(msg);
    }, 450);
}

form.addEventListener('submit', (e) => { e.preventDefault(); submit(); });
$('cv-cancel').addEventListener('click', cancelEdit);
form.addEventListener('keydown', (e) => { if (e.key === 'Escape' && editing !== null) cancelEdit(); });
[inKey, inVal, inDesc].forEach(i => i.addEventListener('input', () => {
    if (!$('cv-error').hidden) hideError();
    refreshForm();
}));

// ---------- UPS Settings: battery.charge.low row follows the custom variable ----------
function syncSettings() {
    const row = $('row-battery.charge.low');
    const v = vars.find(x => x.key === 'battery.charge.low');
    row.classList.toggle('is-overridden', !!v);
    row.querySelector('.set-tags').hidden = !v;
    row.querySelector('.set-ovr-note').hidden = !v;
    row.querySelector('.set-field input').disabled = !!v;
    if (v) $('ovr-value').textContent = v.value;
}

// ---------- Tabs (only NUT Server and UPS Settings are live) ----------
const TITLES = { nut: 'NUT Server', settings: 'UPS Settings' };
function showTab(target) {
    document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.target === target));
    const id = TITLES[target] ? `content-${target}` : 'content-other';
    document.querySelectorAll('.tab-content').forEach(c => c.classList.toggle('active', c.id === id));
    $('page-title').textContent = TITLES[target] || document.querySelector(`.tab[data-target="${target}"]`).textContent.trim();
}
document.querySelectorAll('.tab').forEach(t => t.addEventListener('click', () => showTab(t.dataset.target)));

document.querySelectorAll('[data-goto-key]').forEach(b => b.addEventListener('click', () => {
    const key = b.dataset.gotoKey;
    showTab('nut');
    const row = document.querySelector(`.cv-row[data-key="${key}"]`);
    if (!row) return;
    row.scrollIntoView({ block: 'center', behavior: 'smooth' });
    row.classList.remove('pulse');
    void row.offsetWidth;
    row.classList.add('pulse');
}));

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

    const st = STATES[state];
    vars = st.vars.map(v => ({ ...v }));
    editing = null;
    clearForm(); hideError(); hideOk();
    showTab(st.tab || 'nut');
    renderList();
    if (st.edit) startEdit(st.edit);
    if (st.typed) {
        inKey.value = st.typed.key; inVal.value = st.typed.value; inDesc.value = st.typed.description;
        refreshForm();
    }
    if (st.submit) submit();
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));

// ?embed=1&state=...&scroll=<element id> is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 'list');
if (params.get('scroll')) {
    // scroll only the content area (on phones the sidebar stays on top of it)
    window.addEventListener('load', () => {
        const area = document.querySelector('.content-area');
        const el = $(params.get('scroll'));
        if (el) area.scrollTop += el.getBoundingClientRect().top - area.getBoundingClientRect().top - 16;
    });
}
