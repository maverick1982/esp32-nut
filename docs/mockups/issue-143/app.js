// Issue #143 mockup: custom NUT variables on the UPS Telemetry page. Plain JS, no app code.
// The table is built the same way bundle.js builds it today (sorted keys, 3 columns); the only
// new input is "_overrides" in /api/ups-vars: { key: { value, description } }.

const $ = id => document.getElementById(id);

const NUT_DESCRIPTIONS = {
    'battery.charge': 'Battery charge (percent of full)',
    'battery.charge.low': 'Remaining battery level when UPS switches to LB (percent)',
    'battery.date': 'Battery change date',
    'battery.runtime': 'Battery runtime (seconds)',
    'battery.runtime.low': 'Remaining battery runtime when UPS switches to LB (seconds)',
    'battery.type': 'Battery chemistry',
    'battery.voltage': 'Battery voltage (V)',
    'device.location': 'Device physical location',
    'device.mfr': 'Device manufacturer',
    'device.model': 'Device model',
    'device.serial': 'Device serial number',
    'device.type': 'Device type',
    'driver.name': 'Driver name',
    'input.sensitivity': 'Input power sensitivity',
    'input.transfer.high': 'High voltage transfer point (V)',
    'input.transfer.low': 'Low voltage transfer point (V)',
    'input.voltage': 'Input voltage (V)',
    'output.voltage': 'Output voltage (V)',
    'ups.beeper.status': 'UPS beeper status',
    'ups.firmware': 'UPS firmware',
    'ups.load': 'Load on UPS (percent of full)',
    'ups.mfr': 'UPS manufacturer',
    'ups.model': 'UPS model',
    'ups.realpower': 'Current value of real power (W)',
    'ups.realpower.nominal': 'UPS real power rating (W)',
    'ups.serial': 'UPS serial number',
    'ups.status': 'UPS status',
    'ups.test.result': 'Results of last self test',
};

// What the UPS reports (APC Back-UPS BX950MI): unchanged by #143
const UPS_VARS = {
    'battery.charge': '100', 'battery.charge.low': '10', 'battery.runtime': '2340',
    'battery.runtime.low': '120', 'battery.type': 'PbAc', 'battery.voltage': '13.6',
    'device.mfr': 'American Power Conversion', 'device.model': 'Back-UPS BX950MI',
    'device.serial': '9B2245A12345', 'device.type': 'ups', 'driver.name': 'usbhid-ups',
    'input.sensitivity': 'medium', 'input.transfer.high': '295', 'input.transfer.low': '145',
    'input.voltage': '233.0', 'output.voltage': '230.0', 'ups.beeper.status': 'enabled',
    'ups.firmware': '925.T2 .I', 'ups.load': '18', 'ups.mfr': 'American Power Conversion',
    'ups.model': 'Back-UPS BX950MI', 'ups.realpower.nominal': '520', 'ups.serial': '9B2245A12345',
    'ups.status': 'OL', 'ups.test.result': 'No test initiated',
};

// Custom variables of the NUT Server tab (US-115)
const OVERRIDES = {
    'battery.date': { value: '2025/03/01', description: 'New battery, replaced by the installer' },
    'battery.runtime.low': { value: '300', description: 'Shut the NAS down earlier' },
    'device.location': { value: 'Rack B, basement', description: '' },
    'ups.realpower.nominal': { value: '480', description: 'Rating on the label' },
};
const CARD_OVERRIDE = {
    'ups.realpower': { value: '95', description: 'Measured with a wattmeter' },
};

const ICON_LAYERS = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><polygon points="12 2 2 7 12 12 22 7 12 2"/><polyline points="2 17 12 22 22 17"/><polyline points="2 12 12 17 22 12"/></svg>';
const ICON_PLUS = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" aria-hidden="true"><path d="M12 5v14M5 12h14"/></svg>';
const ICON_EDIT = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M12 20h9"/><path d="M16.5 3.5a2.121 2.121 0 013 3L7 19l-4 1 1-4L16.5 3.5z"/></svg>';
const ICON_DEL = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 01-2 2H8a2 2 0 01-2-2L5 6M10 11v6M14 11v6M9 6V4a1 1 0 011-1h4a1 1 0 011 1v2"/></svg>';

function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    return e;
}

// The chip in the Value column: a button that opens NUT Server on that variable
function chip(key, entry, added) {
    const b = el('button', 'tm-nut ' + (added ? 'add' : 'ovr'));
    b.type = 'button';
    b.dataset.key = key;
    b.title = added
        ? `The UPS does not report ${key}: NUT clients get it from the NUT Server tab`
        : `NUT clients see ${entry.value} in place of the UPS value (NUT Server tab)`;
    b.innerHTML = added ? ICON_PLUS : ICON_LAYERS;
    b.appendChild(el('span', 'k', added ? 'Added' : 'NUT clients'));
    if (!added) b.appendChild(el('span', 'v', entry.value));
    b.addEventListener('click', () => openNutServer(key));
    return b;
}

function renderTable(overrides) {
    const body = $('ups-table-body');
    body.textContent = '';
    const keys = [...new Set([...Object.keys(UPS_VARS), ...Object.keys(overrides)])].sort();
    for (const key of keys) {
        const ups = UPS_VARS[key];
        const ovr = overrides[key];
        const added = ovr && ups === undefined;
        const tr = el('tr');
        tr.id = 'row-' + key.replace(/\./g, '-');
        if (ovr) tr.className = added ? 'tm-add' : 'tm-ovr';

        tr.appendChild(el('td', '', key));

        const tdVal = el('td');
        const val = el('div', 'tm-val');
        if (added) {
            // Added: the value is the custom one (there is no UPS value), tag "Added"
            val.appendChild(el('span', 'ups-val', ovr.value));
            val.appendChild(chip(key, ovr, true));
        } else {
            val.appendChild(el('span', 'ups-val', ups));
            if (ovr) val.appendChild(chip(key, ovr, false));
        }
        tdVal.appendChild(val);
        tr.appendChild(tdVal);

        const tdDesc = el('td');
        tdDesc.appendChild(document.createTextNode(NUT_DESCRIPTIONS[key] || '--'));
        if (ovr && ovr.description) tdDesc.appendChild(el('span', 'tm-why', ovr.description));
        tr.appendChild(tdDesc);

        body.appendChild(tr);
    }
}

function renderSummary(overrides) {
    const keys = Object.keys(overrides);
    const btn = $('tm-summary');
    btn.hidden = keys.length === 0;
    const o = keys.filter(k => UPS_VARS[k] !== undefined).length;
    const a = keys.length - o;
    const t = $('tm-summary-text');
    t.innerHTML = '';
    if (o) { const b = el('b', 'o', `${o} overridden`); t.appendChild(b); }
    if (o && a) t.appendChild(document.createTextNode(' · '));
    if (a) { const b = el('b', 'a', `${a} added`); t.appendChild(b); }
    t.appendChild(document.createTextNode(' for NUT clients'));
}

function renderCards(withCard) {
    $('card-mark-realpower').hidden = !withCard;
}

// NUT Server tab: the US-115 list, read-only in this mockup
function renderNutList(overrides) {
    const list = $('cv-list');
    list.textContent = '';
    const keys = Object.keys(overrides).sort();
    $('cv-count-n').textContent = keys.length;
    for (const key of keys) {
        const entry = overrides[key];
        const added = UPS_VARS[key] === undefined;
        const row = el('div', 'cmd-row cv-row');
        row.dataset.key = key;
        row.id = 'cv-' + key.replace(/\./g, '-');

        const id = el('div', 'cv-id');
        id.appendChild(el('div', 'cmd-name', key));
        const tags = el('div', 'set-tags');
        const tag = el('span', 'set-tag cv-tag ' + (added ? 'add' : 'ovr'));
        if (added) tag.textContent = 'Added';
        else { tag.innerHTML = ICON_LAYERS; tag.appendChild(document.createTextNode('Overrides UPS')); }
        tags.appendChild(tag);
        id.appendChild(tags);

        const val = el('div', 'cv-val');
        val.appendChild(el('span', 'set-current-value', entry.value));
        if (!added) {
            const ups = el('span', 'cv-ups', 'UPS: ');
            ups.appendChild(el('s', '', UPS_VARS[key]));
            val.appendChild(ups);
        }

        const desc = el('div', 'cmd-desc', entry.description);

        const acts = el('div', 'cv-acts');
        for (const [cls, icon, label] of [['edit', ICON_EDIT, 'Edit'], ['del', ICON_DEL, 'Delete']]) {
            const b = el('button', 'cv-act ' + cls);
            b.type = 'button';
            b.title = `${label} ${key}`;
            b.innerHTML = icon;
            acts.appendChild(b);
        }
        row.append(id, val, desc, acts);
        list.appendChild(row);
    }
}

function showTab(target) {
    document.querySelectorAll('.tab').forEach(t => t.classList.toggle('active', t.dataset.target === target));
    document.querySelectorAll('.tab-content').forEach(c => c.classList.toggle('active', c.id === 'content-' + target));
    const tab = document.querySelector(`.tab[data-target="${target}"]`);
    document.querySelector('.page-title').textContent = tab.textContent.trim();
}

// Same path as "Open NUT Server" of UPS Settings (US-115): switch tab, scroll, pulse the row
function openNutServer(key) {
    showTab('nut');
    if (!key) return;
    const row = $('cv-' + key.replace(/\./g, '-'));
    if (!row) return;
    row.scrollIntoView({ block: 'center', behavior: 'smooth' });
    const cls = UPS_VARS[key] === undefined ? 'pulse-add' : 'pulse-ovr';
    row.classList.remove('pulse-add', 'pulse-ovr');
    void row.offsetWidth;
    row.classList.add(cls);
}

const NOTES = {
    custom: 'Overridden rows keep the <b>UPS value</b> in place, with the amber chip <code>NUT clients: X</code>; added rows show the custom value with the cyan <code>Added</code> chip, sorted with the others. Under the description: the note written in NUT Server. Click a chip or the header summary.',
    none: 'No custom variables: the page is exactly as today (no summary chip, no colored rows).',
    card: 'The 4 cards always show the UPS values. With a custom value for <code>ups.status</code>, <code>battery.charge</code>, <code>ups.load</code> or <code>ups.realpower</code> the card gets a small marker; here <code>ups.realpower</code> is added (the UPS does not report it), so the card stays <code>--</code>. <code>ups.status</code> cannot be overridden.',
    nut: 'Click on <code>battery.runtime.low</code>: NUT Server opens and the row pulses, as from UPS Settings. The lead text of the panel changes (<b>underlined</b>).',
    mobile: 'At 1000px and below the table stacks: parameter, value + chip (wraps under the value when long), description + note.',
};

function applyState(state) {
    document.querySelectorAll('.mock-switcher button').forEach(b =>
        b.classList.toggle('on', b.dataset.state === state));
    $('mock-note').innerHTML = NOTES[state] || '';

    const phones = $('phone-stage');
    if (state === 'mobile') {
        $('app').hidden = true;
        phones.hidden = false;
        phones.querySelectorAll('iframe').forEach(f => { if (!f.src) f.src = f.dataset.src; });
        return;
    }
    $('app').hidden = false;
    phones.hidden = true;

    const overrides = state === 'none' ? {} : Object.assign({}, OVERRIDES, state === 'card' ? CARD_OVERRIDE : {});
    renderSummary(overrides);
    renderCards(state === 'card');
    renderTable(overrides);
    renderNutList(overrides);
    if (state === 'nut') setTimeout(() => openNutServer('battery.runtime.low'), 150);
    else showTab('ups');
}

document.querySelectorAll('.mock-switcher button').forEach(b =>
    b.addEventListener('click', () => applyState(b.dataset.state)));
document.querySelectorAll('.tab:not([disabled])').forEach(t =>
    t.addEventListener('click', () => showTab(t.dataset.target)));
$('tm-summary').addEventListener('click', () => openNutServer(null));
$('card-mark-realpower').addEventListener('click', () => openNutServer('ups.realpower'));

// ?embed=1&state=...&scroll=<element id> is used by the phone frames
const params = new URLSearchParams(location.search);
if (params.get('embed') === '1') document.body.classList.add('mock-embed');
applyState(params.get('state') || 'custom');
if (params.get('scroll')) {
    requestAnimationFrame(() => {
        const area = document.querySelector('.content-area');
        const target = $(params.get('scroll'));
        if (area && target) area.scrollTop += target.getBoundingClientRect().top - area.getBoundingClientRect().top - 16;
    });
}
