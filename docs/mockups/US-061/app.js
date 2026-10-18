// US-061 mockup — state switcher and fake form behaviour. No network calls.
(function () {
    'use strict';

    const $ = (id) => document.getElementById(id);

    const NOTES = {
        'signin': '<b>GET /login</b> &middot; credentials exist. Any protected page or a 401 from the API redirects here. Mock: admin / password1 signs in.',
        'signin-error': '<b>POST /api/login &rarr; 401</b> &middot; generic message, never says which field is wrong. Password is cleared, username kept.',
        'signin-expired': '<b>Redirect after expiry</b> &middot; 30 min of inactivity: polling stops and the UI returns to /login with this notice.',
        'create': '<b>GET /login</b> &middot; no credentials stored (e.g. right after OTA). Only this form is shown, nothing else is reachable.',
        'create-errors': '<b>Client-side validation</b> &middot; same rules are enforced by the firmware; a second creation attempt gets an error.',
        'sidebar': '<b>Main app, STA mode</b> &middot; in AP mode there is no login, so the session row is not rendered.'
    };

    const VIEW_OF = {
        'signin': 'signin', 'signin-error': 'signin', 'signin-expired': 'signin',
        'create': 'create', 'create-errors': 'create',
        'sidebar': 'sidebar'
    };

    // ---------- Sign in ----------
    function resetSignin() {
        $('signin-error').hidden = true;
        $('signin-expired').hidden = true;
        $('login-username').value = '';
        $('login-password').value = '';
        setBusy($('btn-signin'), false, 'Sign in');
    }

    function showSigninError() {
        $('signin-error').hidden = false;
        $('login-username').value = 'admin';
        $('login-password').value = '';
        $('login-password').focus({ preventScroll: true });
    }

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

    $('signin-form').addEventListener('submit', (e) => {
        e.preventDefault();
        $('signin-error').hidden = true;
        $('signin-expired').hidden = true;
        const btn = $('btn-signin');
        setBusy(btn, true, 'Signing in');
        // Mock: "admin" / "password1" signs in, anything else is rejected.
        setTimeout(() => {
            setBusy(btn, false, 'Sign in');
            const ok = $('login-username').value === 'admin' && $('login-password').value === 'password1';
            if (ok) setState('sidebar');
            else showSigninError();
        }, 700);
    });

    // ---------- Create credentials ----------
    const printable = /^[\x20-\x7E]+$/;

    function rules() {
        const u = $('new-username').value;
        const p = $('new-password').value;
        const c = $('new-password2').value;
        return {
            user: u.length >= 1 && u.length <= 32 && printable.test(u),
            pass: p.length >= 8 && p.length <= 64,
            match: c.length > 0 && p === c
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

    function validateCreate() {
        const r = paintRules(true);
        setFieldError('new-username', 'err-username', !r.user);
        setFieldError('new-password', 'err-password', !r.pass);
        setFieldError('new-password2', 'err-password2', r.pass && !r.match);
        $('create-error-text').textContent = 'Please fix the highlighted fields.';
        $('create-error').hidden = r.user && r.pass && r.match;
        return r.user && r.pass && r.match;
    }

    function resetCreate() {
        ['new-username', 'new-password', 'new-password2'].forEach((id) => { $(id).value = ''; });
        setFieldError('new-username', 'err-username', false);
        setFieldError('new-password', 'err-password', false);
        setFieldError('new-password2', 'err-password2', false);
        $('create-error').hidden = true;
        paintRules(false);
        setBusy($('btn-create'), false, 'Create and sign in');
    }

    function showCreateErrors() {
        $('new-username').value = 'admin';
        $('new-password').value = 'ups123';
        $('new-password2').value = 'ups1234';
        // Show both messages from the spec: too short + do not match
        paintRules(true);
        setFieldError('new-username', 'err-username', false);
        setFieldError('new-password', 'err-password', true);
        setFieldError('new-password2', 'err-password2', true);
        $('create-error-text').textContent = 'Please fix the highlighted fields.';
        $('create-error').hidden = false;
    }

    ['new-username', 'new-password', 'new-password2'].forEach((id) => {
        $(id).addEventListener('input', () => {
            paintRules(false);
            // clear a field's error as soon as it becomes valid
            const r = rules();
            if (r.user) setFieldError('new-username', 'err-username', false);
            if (r.pass) setFieldError('new-password', 'err-password', false);
            if (r.match) setFieldError('new-password2', 'err-password2', false);
            if (r.user && r.pass && r.match) $('create-error').hidden = true;
        });
    });

    $('create-form').addEventListener('submit', (e) => {
        e.preventDefault();
        if (!validateCreate()) return;
        const btn = $('btn-create');
        setBusy(btn, true, 'Creating');
        setTimeout(() => {
            setBusy(btn, false, 'Create and sign in');
            setState('sidebar');
        }, 800);
    });

    // ---------- Show / hide password ----------
    document.querySelectorAll('[data-toggle]').forEach((b) => {
        b.addEventListener('click', () => {
            const input = $(b.dataset.toggle);
            const show = input.type === 'password';
            input.type = show ? 'text' : 'password';
            b.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
            b.style.color = show ? 'var(--accent)' : '';
        });
    });

    // ---------- Log out ----------
    $('btn-logout').addEventListener('click', () => setState('signin'));

    // ---------- State switcher ----------
    function setState(state) {
        document.querySelectorAll('.mock-view').forEach((v) => {
            v.hidden = v.dataset.view !== VIEW_OF[state];
        });
        document.querySelectorAll('.mock-switcher button').forEach((b) =>
            b.classList.toggle('on', b.dataset.state === state));
        $('mock-note').innerHTML = NOTES[state];

        resetSignin();
        resetCreate();
        if (state === 'signin-error') showSigninError();
        if (state === 'signin-expired') $('signin-expired').hidden = false;
        if (state === 'create-errors') showCreateErrors();
    }

    document.querySelectorAll('.mock-switcher button').forEach((b) =>
        b.addEventListener('click', () => setState(b.dataset.state)));

    setState('signin');
})();
