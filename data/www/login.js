// US-061: /login page. "Sign in" when credentials exist, "Create credentials" on first use.
(function () {
    'use strict';

    const $ = (id) => document.getElementById(id);
    const JSON_HEADERS = { 'Content-Type': 'application/json' };
    const DEFAULT_SIGNIN_ERROR = 'Invalid username or password';
    const DEFAULT_CREATE_ERROR = 'Please fix the highlighted fields.';
    const FIRST_USE_LEAD = $('create-lead').textContent;
    // US-091: double reset on a board without Wi-Fi, new credentials replace the current ones
    let recoveryMode = false;
    const createLabel = () => (recoveryMode ? 'Replace and sign in' : 'Create and sign in');
    const createBusyLabel = () => (recoveryMode ? 'Replacing' : 'Creating');

    function applyCreateMode(recovery, remainingS) {
        recoveryMode = recovery;
        $('create-title').textContent = recovery ? 'Reset credentials' : 'Create credentials';
        $('create-badge').textContent = recovery ? 'Recovery' : 'First use';
        if (recovery) {
            const min = Math.max(1, Math.ceil((Number(remainingS) || 0) / 60));
            $('create-lead').textContent = 'A double reset opened a recovery window. New credentials replace ' +
                'the current ones and sign out every device. The window closes in ' + min + '\u00a0min.';
        } else {
            $('create-lead').textContent = FIRST_USE_LEAD;
        }
        $('recovery-signin').hidden = !recovery;
        $('btn-create').querySelector('.btn-label').textContent = createLabel();
    }

    function goHome() {
        window.location.replace('/');
    }

    function showView(name) {
        $('view-signin').hidden = name !== 'signin';
        $('view-create').hidden = name !== 'create';
        const focusId = name === 'signin' ? 'login-username' : 'new-username';
        $(focusId).focus({ preventScroll: true });
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

    async function postJson(url, body) {
        const res = await fetch(url, {
            method: 'POST',
            headers: JSON_HEADERS,
            body: JSON.stringify(body)
        });
        let data = {};
        try {
            data = await res.json();
        } catch (e) {
            // empty or non-JSON body: the status code is enough
        }
        return { status: res.status, data: data || {}, headers: res.headers };
    }

    // ---------- Status ----------
    async function loadStatus() {
        let status = {};
        try {
            const res = await fetch('/api/auth/status', { cache: 'no-store' });
            if (res.ok) status = (await res.json()) || {};
        } catch (e) {
            console.error('Failed to fetch auth status:', e);
        }
        if (status.authenticated === true) {
            goHome();
            return;
        }
        applyCreateMode(status.recovery === true, status.recovery_remaining_s);
        if (recoveryMode) {
            showView('create');
            return;
        }
        // Only an explicit "configured: false" opens the first-use form; when in
        // doubt the sign-in form is shown and a 409 from the login switches over.
        showView(status.configured === false ? 'create' : 'signin');
    }

    // ---------- Sign in ----------
    function showSigninError(text) {
        $('signin-error-text').textContent = text || DEFAULT_SIGNIN_ERROR;
        $('signin-error').hidden = false;
    }

    // US-062: 429 after too many failed attempts. The wait comes from the body
    // (retry_after, seconds) or from the Retry-After header; "Sign in" stays
    // disabled while the countdown runs.
    let throttleTimer = null;

    function retryAfterSeconds(result) {
        let n = Number(result.data.retry_after);
        if (!Number.isFinite(n) || n <= 0) {
            n = Number(result.headers && result.headers.get('Retry-After'));
        }
        return Number.isFinite(n) && n > 0 ? Math.ceil(n) : 1;
    }

    function startThrottle(seconds) {
        const btn = $('btn-signin');
        let left = seconds;
        if (throttleTimer) clearInterval(throttleTimer);
        btn.disabled = true;
        showSigninError(`Too many attempts. Try again in ${left} s`);
        throttleTimer = setInterval(() => {
            left--;
            if (left > 0) {
                $('signin-error-text').textContent = `Too many attempts. Try again in ${left} s`;
                return;
            }
            clearInterval(throttleTimer);
            throttleTimer = null;
            $('signin-error').hidden = true;
            btn.disabled = false;
        }, 1000);
    }

    $('signin-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        if (throttleTimer) return;
        const user = $('login-username');
        const pwd = $('login-password');
        if (!user.value) { user.focus(); return; }
        if (!pwd.value) { pwd.focus(); return; }

        $('signin-error').hidden = true;
        $('signin-expired').hidden = true;
        const btn = $('btn-signin');
        setBusy(btn, true, 'Signing in');
        let result;
        try {
            result = await postJson('/api/auth/login', {
                username: user.value,
                password: pwd.value,
                remember: $('login-remember').checked === true
            });
        } catch (err) {
            console.error('Login request failed:', err);
            setBusy(btn, false, 'Sign in');
            showSigninError('Network error, please try again');
            return;
        }
        if (result.status === 200) {
            goHome();
            return;
        }
        setBusy(btn, false, 'Sign in');
        if (result.status === 409) {
            // No credentials on the device (e.g. reset meanwhile): first-use form
            resetCreate();
            showView('create');
            return;
        }
        if (result.status === 429) {
            startThrottle(retryAfterSeconds(result));
            pwd.value = '';
            return;
        }
        // 401 and anything else: generic message, never which field is wrong
        showSigninError(result.status === 401 || result.status === 400
            ? DEFAULT_SIGNIN_ERROR
            : `Sign in failed (HTTP ${result.status})`);
        pwd.value = '';
        pwd.focus({ preventScroll: true });
    });

    // ---------- Create credentials ----------
    // Same rules as the firmware: username 1-32 printable ASCII characters,
    // password 8-64 bytes.
    const printable = /^[\x20-\x7E]+$/;

    function byteLength(s) {
        return new TextEncoder().encode(s).length;
    }

    function rules() {
        const u = $('new-username').value;
        const p = $('new-password').value;
        const c = $('new-password2').value;
        const pLen = byteLength(p);
        return {
            user: u.length >= 1 && u.length <= 32 && printable.test(u),
            pass: pLen >= 8 && pLen <= 64,
            passTooLong: pLen > 64,
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

    function showCreateError(text) {
        $('create-error-text').textContent = text || DEFAULT_CREATE_ERROR;
        $('create-error').hidden = false;
    }

    function validateCreate() {
        const r = paintRules(true);
        $('err-password').textContent = r.passTooLong
            ? 'Password is too long (maximum 64 bytes: accented letters and emoji take 2-4 bytes each)'
            : 'Password is too short (minimum 8 characters)';
        setFieldError('new-username', 'err-username', !r.user);
        setFieldError('new-password', 'err-password', !r.pass);
        setFieldError('new-password2', 'err-password2', r.pass && !r.match);
        const ok = r.user && r.pass && r.match;
        if (ok) $('create-error').hidden = true;
        else showCreateError(DEFAULT_CREATE_ERROR);
        return ok;
    }

    function resetCreate() {
        ['new-username', 'new-password', 'new-password2'].forEach((id) => { $(id).value = ''; });
        setFieldError('new-username', 'err-username', false);
        setFieldError('new-password', 'err-password', false);
        setFieldError('new-password2', 'err-password2', false);
        $('create-error').hidden = true;
        paintRules(false);
        setBusy($('btn-create'), false, createLabel());
    }

    ['new-username', 'new-password', 'new-password2'].forEach((id) => {
        $(id).addEventListener('input', () => {
            const r = paintRules(false);
            // clear a field's error as soon as it becomes valid
            if (r.user) setFieldError('new-username', 'err-username', false);
            if (r.pass) setFieldError('new-password', 'err-password', false);
            if (r.match) setFieldError('new-password2', 'err-password2', false);
            if (r.user && r.pass && r.match) $('create-error').hidden = true;
        });
    });

    $('create-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        if (!validateCreate()) return;
        const btn = $('btn-create');
        setBusy(btn, true, createBusyLabel());
        let result;
        try {
            result = await postJson('/api/auth/credentials', {
                username: $('new-username').value,
                password: $('new-password').value
            });
        } catch (err) {
            console.error('Create credentials request failed:', err);
            setBusy(btn, false, createLabel());
            showCreateError('Network error, please try again');
            return;
        }
        if (result.status === 200) {
            goHome();
            return;
        }
        setBusy(btn, false, createLabel());
        // Credentials created meanwhile: in STA the request becomes a change, so
        // without a session the firmware answers 401 unauthorized (US-063); 409
        // already_configured is still handled for the internal creation path
        if (result.status === 409 || result.status === 401) {
            // Back to the real state (Sign in); in recovery the window closed meanwhile
            const wasRecovery = recoveryMode;
            resetCreate();
            await loadStatus();
            if (!$('view-signin').hidden) {
                showSigninError(wasRecovery ? 'The recovery window has closed. Please sign in.'
                    : 'Credentials have already been created. Please sign in.');
            }
            return;
        }
        if (result.status === 400 && result.data.error === 'invalid_username') {
            setFieldError('new-username', 'err-username', true);
            showCreateError(DEFAULT_CREATE_ERROR);
        } else if (result.status === 400 && result.data.error === 'invalid_password') {
            setFieldError('new-password', 'err-password', true);
            showCreateError(DEFAULT_CREATE_ERROR);
        } else {
            showCreateError(`Could not create the credentials (HTTP ${result.status})`);
        }
    });

    // US-091: during the recovery window the current credentials still work
    $('link-signin').addEventListener('click', () => showView('signin'));

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

    // ---------- Start ----------
    if (new URLSearchParams(window.location.search).get('expired') === '1') {
        $('signin-expired').hidden = false;
    }
    loadStatus();
})();
