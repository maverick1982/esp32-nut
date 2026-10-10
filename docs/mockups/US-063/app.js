// US-063 mockup: state switcher and fake form behaviour. No network calls.
// Mock: the current password is "password1"; anything else is "wrong".
(function () {
    'use strict';

    const $ = (id) => document.getElementById(id);

    const NOTES = {
        'sta': '<b>STA, signed in</b> &middot; POST /api/auth/credentials needs a valid session + the current password. Mock: current password is <b>password1</b>.',
        'sta-wrong': '<b>POST &rarr; 401</b> &middot; wrong current password. Counts as a failed attempt (same limit as /login). Current password cleared, new fields kept.',
        'sta-429': '<b>POST &rarr; 429</b> &middot; throttled like the login. The button stays disabled until the countdown ends.',
        'sta-saved': '<b>POST &rarr; 200</b> &middot; every other session is closed, this one stays open. Sidebar shows the new username.',
        'ap': '<b>AP mode</b> &middot; no login, no session row. The firmware accepts the request without current password only when really in AP.',
        'ap-done': '<b>AP reset &rarr; 200</b> &middot; all sessions closed. Back in STA (restart) you sign in with the new credentials.'
    };

    let mode = 'sta';          // 'sta' | 'ap'
    let countdownTimer = null;

    // ---------- helpers ----------
    function setBusy(btn, busy, label) {
        btn.disabled = busy;
        btn.querySelector('.btn-label').textContent = busy ? label + '…' : label;
        const sp = btn.querySelector('.spinner');
        if (busy && !sp) {
            const s = document.createElement('span');
            s.className = 'spinner';
            btn.insertBefore(s, btn.querySelector('.btn-label'));
        } else if (!busy && sp) {
            sp.remove();
        }
    }

    const btnLabel = () => (mode === 'ap' ? 'Reset credentials' : 'Save credentials');

    function showError(html) {
        $('cred-error-text').innerHTML = html;
        $('cred-error').hidden = false;
        $('cred-success').hidden = true;
    }

    function showSuccess(text) {
        $('cred-success-text').textContent = text;
        $('cred-success').hidden = false;
        $('cred-error').hidden = true;
    }

    function clearAlerts() {
        $('cred-error').hidden = true;
        $('cred-success').hidden = true;
        stopCountdown();
    }

    // ---------- validation (same rules as US-061) ----------
    const printable = /^[\x20-\x7E]+$/;
    const bytes = (s) => new TextEncoder().encode(s).length;

    function rules() {
        const u = $('cred-username').value;
        const p = $('cred-password').value;
        const c = $('cred-password2').value;
        return {
            user: u.length >= 1 && u.length <= 32 && printable.test(u),
            pass: bytes(p) >= 8 && bytes(p) <= 64,
            match: c.length > 0 && p === c,
            current: mode === 'ap' || $('cur-password').value.length > 0
        };
    }

    function paintRules(showBad) {
        const r = rules();
        document.querySelectorAll('#rule-list li').forEach((li) => {
            const ok = r[li.dataset.rule];
            li.classList.toggle('ok', ok);
            li.classList.toggle('bad', !ok && showBad);
        });
        return r;
    }

    function setFieldError(inputId, errId, show) {
        $(inputId).classList.toggle('invalid', show);
        $(errId).hidden = !show;
    }

    function clearFieldErrors() {
        setFieldError('cur-password', 'err-current', false);
        setFieldError('cred-username', 'err-username', false);
        setFieldError('cred-password', 'err-password', false);
        setFieldError('cred-password2', 'err-password2', false);
    }

    function validate() {
        const r = paintRules(true);
        setFieldError('cur-password', 'err-current', !r.current);
        setFieldError('cred-username', 'err-username', !r.user);
        setFieldError('cred-password', 'err-password', !r.pass);
        setFieldError('cred-password2', 'err-password2', r.pass && !r.match);
        const ok = r.current && r.user && r.pass && r.match;
        if (!ok) showError('Please fix the highlighted fields.');
        return ok;
    }

    ['cur-password', 'cred-username', 'cred-password', 'cred-password2'].forEach((id) => {
        $(id).addEventListener('input', () => {
            paintRules(false);
            const r = rules();
            if (r.current) setFieldError('cur-password', 'err-current', false);
            if (r.user) setFieldError('cred-username', 'err-username', false);
            if (r.pass) setFieldError('cred-password', 'err-password', false);
            if (r.match) setFieldError('cred-password2', 'err-password2', false);
            $('cred-success').hidden = true;
        });
    });

    // ---------- 429 countdown ----------
    function startCountdown(seconds) {
        stopCountdown();
        let left = seconds;
        const btn = $('btn-cred');
        btn.disabled = true;
        const paint = () => showError('Too many attempts. Try again in <span class="countdown">' + left + '</span> s');
        paint();
        countdownTimer = setInterval(() => {
            left -= 1;
            if (left <= 0) {
                stopCountdown();
                $('cred-error').hidden = true;
                return;
            }
            paint();
        }, 1000);
    }

    function stopCountdown() {
        if (countdownTimer) clearInterval(countdownTimer);
        countdownTimer = null;
        $('btn-cred').disabled = false;
    }

    // ---------- submit ----------
    let failures = 0;

    $('cred-form').addEventListener('submit', (e) => {
        e.preventDefault();
        if (countdownTimer) return;
        $('cred-success').hidden = true;
        if (!validate()) return;
        $('cred-error').hidden = true;

        const btn = $('btn-cred');
        setBusy(btn, true, mode === 'ap' ? 'Resetting' : 'Saving');
        setTimeout(() => {
            setBusy(btn, false, btnLabel());
            if (mode === 'sta' && $('cur-password').value !== 'password1') {
                failures += 1;
                if (failures >= 5) { failures = 0; showWrongThrottled(); }
                else showWrong();
                return;
            }
            failures = 0;
            if (mode === 'ap') showApDone();
            else showSaved();
        }, 800);
    });

    // ---------- canned states ----------
    function showWrong() {
        showError('Current password is wrong');
        $('cur-password').value = '';
        $('cur-password').classList.add('invalid');
        $('cur-password').focus({ preventScroll: true });
    }

    function showWrongThrottled() {
        $('cur-password').value = '';
        startCountdown(30);
    }

    function fillNew(user) {
        $('cred-username').value = user;
        $('cred-password').value = 'n3w-Passw0rd';
        $('cred-password2').value = 'n3w-Passw0rd';
    }

    function clearPasswords() {
        ['cur-password', 'cred-password', 'cred-password2'].forEach((id) => { $(id).value = ''; });
        paintRules(false);
    }

    function showSaved() {
        const user = $('cred-username').value;
        clearPasswords();
        $('session-username').textContent = user;
        showSuccess('Credentials saved. Other devices have been signed out.');
    }

    function showApDone() {
        clearPasswords();
        showSuccess('Credentials reset. All sessions have been closed.');
    }

    // ---------- mode (STA / AP) ----------
    function applyMode(m) {
        mode = m;
        const ap = m === 'ap';
        $('grp-current').hidden = ap;
        $('sec-verify').hidden = ap;
        $('sec-new').hidden = ap;
        $('cred-ap-banner').hidden = !ap;
        $('cred-badge').hidden = !ap;
        $('cred-lead').hidden = ap;
        $('sidebar-session').hidden = ap;
        $('cred-fineprint').innerHTML = ap
            ? 'Resetting closes every session on every device. NUT credentials are not affected (<button type="button" class="inline-link" data-goto="nut">NUT Server tab</button>). The NUT server is not running in Access Point mode.'
            : 'Saving signs out every other device. NUT credentials are not affected (<button type="button" class="inline-link" data-goto="nut">NUT Server tab</button>).';
        $('ind-wifi').className = 'status-indicator ' + (ap ? 'info' : 'success');
        $('lbl-wifi').textContent = ap ? 'Wi-Fi: AP Mode Active' : 'Wi-Fi: Connected';
        setBusy($('btn-cred'), false, btnLabel());
    }

    // ---------- show / hide password ----------
    document.querySelectorAll('[data-toggle]').forEach((b) => {
        b.addEventListener('click', () => {
            const input = $(b.dataset.toggle);
            const show = input.type === 'password';
            input.type = show ? 'text' : 'password';
            b.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
            b.style.color = show ? 'var(--accent)' : '';
        });
    });

    // ---------- tabs (only Web Login is mocked) ----------
    function openTab(target) {
        const tab = document.querySelector('.tab[data-target="' + target + '"]');
        document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t === tab));
        $('page-title').textContent = tab.dataset.title;
        const isCred = target === 'weblogin';
        $('content-weblogin').classList.toggle('active', isCred);
        $('content-stub').classList.toggle('active', !isCred);
        $('stub-caption').textContent = tab.dataset.title + ': unchanged tab, not part of this mockup.';
    }

    document.querySelectorAll('.tab').forEach((t) =>
        t.addEventListener('click', () => openTab(t.dataset.target)));
    document.addEventListener('click', (e) => {
        const link = e.target.closest('[data-goto]');
        if (link) openTab(link.dataset.goto);
    });

    // ---------- state switcher ----------
    function setState(state) {
        document.querySelectorAll('.mock-switcher button').forEach((b) =>
            b.classList.toggle('on', b.dataset.state === state));
        $('mock-note').innerHTML = NOTES[state];

        openTab('weblogin');
        clearAlerts();
        clearFieldErrors();
        failures = 0;
        $('session-username').textContent = 'admin';
        applyMode(state.startsWith('ap') ? 'ap' : 'sta');
        $('cred-username').value = 'admin';
        clearPasswords();

        if (state === 'sta-wrong') {
            fillNew('admin');
            paintRules(false);
            showWrong();
        }
        if (state === 'sta-429') {
            fillNew('admin');
            paintRules(false);
            showWrongThrottled();
        }
        if (state === 'sta-saved') {
            $('cred-username').value = 'nut-admin';
            showSaved();
        }
        if (state === 'ap-done') showApDone();
    }

    document.querySelectorAll('.mock-switcher button').forEach((b) =>
        b.addEventListener('click', () => setState(b.dataset.state)));

    setState('sta');
})();
