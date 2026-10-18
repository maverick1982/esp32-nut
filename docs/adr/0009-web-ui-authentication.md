---
type: decision
title: "Web UI Authentication: Session Table, PBKDF2 Credentials and AP Recovery"
description: "Documento ADR: Web UI Authentication: Session Table, PBKDF2 Credentials and AP Recovery"
tags: [adr, decision, security, web-ui, authentication]
---
# Web UI Authentication: Session Table, PBKDF2 Credentials and AP Recovery

* **ADR ID:** 0009
* **Status:** Proposed
* **Date:** 2026-10-10
* **Authors:** @maverick1982, Claude
* **Product decisions:** [PDR 0001](../pdr/0001-autenticazione-web.md)

## Context and Problem Statement

The web UI (`src/network/web_config_server.cpp`, Arduino `WebServer` on port 80) has no authentication. Anyone on the LAN can:
- read the configuration and logs;
- change Wi-Fi and NUT settings;
- upload firmware on `/update`.

For this reason the web UI refuses destructive UPS commands with 403 (US-059). PDR 0001 requires a form login with "remember me" on every route in STA mode, first-use credential creation (TOFU), a single credentials form (current password required in STA, not in AP), recovery through the manual AP mode, and a timeout for that mode.

How should credentials, sessions and the route guard be implemented, given these platform constraints?

* **No wall clock.** No RTC, and the firmware has no SNTP. The LAN may also have no Internet access. Only `millis()` since boot is reliable.
* **One loop.** `WebServer` is synchronous and is serviced in the same loop as the NUT server. Any blocking work in a handler stalls the NUT clients.
* **No middleware.** `WebServer` has none. Request headers are only visible when listed in `collectHeaders()` (today only `Content-Type`).
* **Upload order.** The `/update` upload callback runs, and starts writing flash, **before** the route's final handler.
* **Flash.** The flash is not encrypted: NVS can be read by anyone holding the board.
* **Crypto.** mbedTLS (IDF 5.1) provides PBKDF2-HMAC-SHA256 and SHA-256, with hardware acceleration on the ESP32-S3. `esp_random()` is a hardware RNG while Wi-Fi is on.
* **AP entry.** The AP mode starts on a double reset within 3 s (NVS flag `boot_state/manual_ap`), or when `config_json` is missing or invalid (`src/core/main.cpp`).

## Considered Options

**Password storage**
* **P1:** Reversible encryption with a key in flash. The key sits next to the ciphertext, so this offers no real protection.
* **P2:** Plain SHA-256 with salt. Too fast against brute force on a stolen dump.
* **P3:** PBKDF2-HMAC-SHA256 with a random salt and a stored iteration count.

**Sessions**
* **S1:** Stateless HMAC-signed token with expiry inside. Expiry needs a wall clock, and a single token cannot be revoked without rotating the key for everyone.
* **S2:** Random opaque token. The server keeps a session table: volatile sessions in RAM, persistent ("remember me") sessions mirrored in NVS.
* **S3:** HTTP Basic/Digest auth. There is no form, no "remember me" and no logout, and the browser caches the credentials.

**Transport**
* **T1:** HTTPS via `esp_https_server`. Needs a self-signed certificate (browser warning), about 40 KB of RAM per TLS connection, and a rewrite of the web server.
* **T2:** Plain HTTP on a trusted LAN, documented.

## Decision Outcome

Chosen options: **P3**, **S2** and **T2**.
- PBKDF2 is the standard slow hash available in mbedTLS.
- Opaque tokens with a session table give per-device logout and need no clock.
- HTTPS costs too much for a LAN appliance.

### Credential store (`AuthStore`, pure logic, unit tested on native)

**Storage**
* NVS namespace `web_auth`, separate from `config_json`. Saving or invalidating the Wi-Fi/NUT config never touches the credentials, and the reverse is also true.
* Keys: `cred` (one blob: version, iterations uint32 LE, salt 16 bytes, hash 32 bytes, username length and username) and `sessions` (blob). The credential record is a single blob because NVS writes one key atomically: a change interrupted by a power loss keeps the previous record instead of mixing username, salt and hash of two versions.
* `hasCredentials()` is false when `cred` is missing or malformed. That is the TOFU state, and also the state after a full flash erase.

**Hashing and validation**
* Hash: `mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, ...)`, 32-byte output.
* The default iteration count is chosen by measurement on the ESP32-S3, so that one verification takes about 100-200 ms. The count is stored per record, so it can be raised later without invalidating existing passwords.
* The comparison is constant-time.
* Validation: username 1-32 printable ASCII characters; password 8-64 bytes.

**Sessions (`SessionTable`)**
* Token: 16 bytes from `esp_random()`, hex-encoded in cookie `sid`. The table stores only `SHA-256(token)`, so an NVS dump yields no live cookie.
* **Volatile** sessions (no "remember me"):
  - RAM only, up to 8;
  - each holds `last_seen_ms`, and expires after 30 min without requests;
  - all are lost on reboot (intended, PDR D2).
* **Persistent** sessions ("remember me"):
  - up to 8, held in RAM and written to the `sessions` blob only on login, logout, logout-all and password change, never on ordinary requests, so there is no flash wear;
  - when the table is full, the oldest entry is evicted;
  - **absolute expiry is not enforced server-side** (no clock). The 30 days come from the cookie `Max-Age`. Server-side revocation is by logout, logout-all, password change, eviction, or a credential reset in AP.
* Credential change: all sessions are cleared, the caller's session is kept in STA, and in AP all of them are cleared.

**Login throttling (`LoginThrottle`)**
* One global counter, not per IP.
* After 5 consecutive failures, further attempts within the backoff window (1 s doubling up to 60 s, timed with `millis()`) get **429 with `Retry-After`** without running PBKDF2.
* The handler never sleeps: a `delay()` would stall NUT.
* A success resets the counter.

### HTTP layer (`web_config_server.cpp`)

**Headers**
* `collectHeaders` gains `Cookie`.
* Cookies are set as `sid=<hex>; Path=/; HttpOnly; SameSite=Strict`, plus `Max-Age=2592000` for "remember me". Logout sends `Max-Age=0`.

**Guard**
* Every `server.on()` handler is registered through one wrapper, `guarded(handler)`. A route can only be public if it is listed explicitly.
* In STA without a valid session:
  - `/api/*` → `401 {"error":"unauthorized"}`, or `{"error":"setup_required"}` while no credentials exist;
  - page routes → `302` to `/login`.
* In AP mode the guard lets everything through (PDR D7).
* A valid volatile session refreshes its `last_seen_ms`.

**OTA**
* The `/update` **upload callback** checks the session at `UPLOAD_FILE_START`. If the request is unauthenticated, it marks the request rejected and never calls `Update.begin()`. The final handler then returns 401.
* Checking only in the final handler would be too late: the flash would already have been written.

**Public routes**
* `GET /login`: a self-contained page, inlined by the existing `scripts/embed_web.py` pipeline like `update.html`.
* `POST /api/auth/login` `{username, password, remember}` → 200 with `Set-Cookie`, 401, or 429.
* `GET /api/auth/status` → `{configured, authenticated, username, ap_mode}`. It never reveals whether a username exists.
* `POST /api/auth/logout` (current session). Public and idempotent: it removes the session named by the cookie, if any, and always answers 200 with the clearing cookie, so an already expired session can still log out cleanly. It cannot affect another session, because it only acts on the caller's own cookie (`SameSite=Strict`, JSON content type required).

**Authenticated routes**
* `POST /api/auth/logout-all`. It needs a valid session and is refused with 403 in AP mode, where sessions are not validated.

**`POST /api/auth/credentials` `{current_password?, username, password}`**
This one endpoint serves PDR D3 and D5:
* no credentials stored → public, `current_password` ignored (TOFU). After creation, a session is created and returned. Once credentials exist, a creation request is no longer a creation: in STA it becomes a change, so without a valid session it gets 401 `unauthorized` (a change needs a session and the current password); in AP it is a reset. 409 `already_configured` remains only on the internal `createInitialCredentials` path;
* AP mode → public, `current_password` ignored (reset);
* STA with credentials → requires a session **and** a correct `current_password` (PBKDF2, throttled like login). After the change the caller's session is rotated: a new token of the same kind (persistent keeps `Max-Age`) is returned in `Set-Cookie` and every other session, the old token included, is closed. The new session and the closing are saved in one storage write; if it fails, no new cookie is sent and the old session stays valid.
* Responses: on change/reset the success body is `{"success":true,"username":...,"sessions_closed":bool}` (`false` when the credentials changed but the other sessions could not be closed; in AP the UI asks to repeat the reset, since "Log out of all devices" is not available there); with credentials configured, an STA request without a valid session gets 401 `unauthorized`.

The AP/STA decision uses the server-side `is_ap_mode`, never a request field.

**CSRF and DNS rebinding**
* `SameSite=Strict` blocks cross-site cookie use.
* All JSON POSTs require `Content-Type: application/json`, as `/api/ups/command` already does: a cross-origin request must then go through a CORS preflight, which the server does not answer.
* DNS rebinding reaches the device without the cookie and gets 401.

**Destructive commands**
* `runUpsCommand()` gets an `allow_destructive` argument: true in STA, where the guard has already authenticated the caller, and false in AP.
* The 403 path stays for AP mode.
* `GET /api/ups/commands` reports per command whether it can run in the current mode, so the page draws the lock only when it applies.

### Frontend (`data/www`)

* A shared `apiFetch` wrapper:
  - on 401 it stops polling and goes to `/login`;
  - on `setup_required` it shows the credentials form.
* `/login` has the form with the "remember me" checkbox and handles 429 by showing the wait time.
* The credentials form hides `current_password` when `ap_mode` is true, otherwise requires it.
* The header has a Logout button and "log out of all devices".

### Manual AP timeout (`main.cpp`, `ApIdleTimeout`)

* It applies only when the AP was started by the double reset **and** `config_ok` is true. With missing or invalid config the AP stays up.
* While `WiFi.softAPgetStationNum() > 0`, the idle start is reset. After 10 min with no station, the loop logs the reason and calls `esp_restart()`. The reason is kept in RTC memory with `CrashDiag::recordRestartReason`, so after the boot it is logged and shown in the web UI as the last controlled restart; `recordControlledRestart` is not used, since it would count as a USB recovery restart and delay the next one. The station count is read once per second. The `manual_ap` flag is already cleared at that point, so the board boots in STA.
* The idle logic is a small pure class (`lib/ApIdleTimeout`) with an injected clock, unit tested like `LinkMonitor` (ADR 0008).

## Consequences

### Positive
* Every web function, including OTA, needs authentication in STA. OTA is blocked before any flash write.
* "Remember me" survives reboots and OTA without a clock or a signing key. Each session can be revoked.
* A flash dump reveals neither the password (PBKDF2) nor usable cookies (hashed tokens).
* Upgraded boards are not locked out (TOFU), and recovery needs no new mechanism (manual AP).
* No new third-party dependency: mbedTLS and NVS are already in the platform.
* Login throttling never blocks the loop, so NUT clients are not affected.

### Negative
* Credentials and cookies travel in cleartext on the LAN, so a sniffed cookie is a full session until revoked.
* The 30-day limit of persistent sessions is enforced only by the browser. A stolen persistent cookie stays valid until logout-all, a password change, eviction, or an AP reset.
* The TOFU window lets the first LAN client after an upgrade claim the device.
* A PBKDF2 verification blocks the loop for about 100-200 ms per login attempt. This is bounded by throttling.
* The AP mode (fixed password `12345678`) is an unauthenticated full-access channel for anyone in radio range while it is active. It is limited by the physical double reset and by the 10-min idle timeout.
* Persistent sessions are capped at 8: a 9th device evicts the oldest.

## Impact on Agent Implementation
* **Routes.** Never register a web route directly with `server.on()`. Use the guard wrapper, and add a route to the public list only if PDR 0001 makes it public.
* **AP or STA.** Never decide based on a request field. Use only the server-side `is_ap_mode`.
* **Secrets.** Never store or log the password or a session token in clear. Never return the password hash, salt or tokens from any API. Log usernames and failure counts only.
* **Blocking.** Never `delay()` in an HTTP handler for throttling: reject with 429 instead.
* **OTA.** Any new upload route must authenticate in its upload callback, before writing anything.
* **NVS.** Do not write the session blob on ordinary requests: only on login, logout, logout-all and credential changes.
* **Credentials.** Keep web credentials separate from NUT credentials: no shared storage, no "same as NUT" option.
* **Tests.**
  - `AuthStore`, `SessionTable`, `LoginThrottle` and the AP idle timer are pure logic with injected clock and RNG, and must have native Unity tests.
  - The guard and frontend flows (login, 401 redirect, TOFU, credentials form in STA/AP, 429) must have Playwright e2e coverage, through `e2e/support/webui.ts` mocks.
