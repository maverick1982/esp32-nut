#include "core/update_service.h"
#include "core/app_logger.h"
#include "network/https_stream.h"
#include "network/network_manager.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <mbedtls/sha256.h>

#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif
#ifndef NUT_BUILD_ENV
#define NUT_BUILD_ENV ""
#endif
#ifdef NUT_RELEASE_SUFFIX
static const char* kReleaseSuffix = NUT_RELEASE_SUFFIX;
#else
static const char* kReleaseSuffix = nullptr;
#endif
#ifndef NUT_UPDATE_REPO
#define NUT_UPDATE_REPO "maverick1982/esp32-nut"
#endif

static const char* kNamespace = "update";
static const char* kKeyAuto = "auto";
static const size_t MIN_FREE_HEAP = 95 * 1024;
static const size_t MIN_FREE_BLOCK = 36 * 1024;

static String userAgent() {
    return String("esp32-nut/") + FIRMWARE_VERSION;
}

void UpdateService::begin(const AppNetworkManager* network) {
    _network = network;
    Preferences prefs;
    bool auto_check = true;
    if (prefs.begin(kNamespace, true)) {
        auto_check = prefs.getBool(kKeyAuto, true);
        prefs.end();
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _status = UpdateStatus();
        _status.current = FIRMWARE_VERSION;
        _status.asset_size = 0;
        _status.asset_verified = false;
        _status.checked = false;
        _status.last_check_ok = false;
        _status.checked_age_s = 0;
        _status.phase = UpdatePhase::Idle;
        _status.progress = 0;
        _status.auto_check = auto_check;
        _schedule.setAuto(auto_check);
    }
    xTaskCreatePinnedToCore(taskEntry, "fw_update", 12288, this, 1, &_task, tskNO_AFFINITY);
}

UpdateStatus UpdateService::status() const {
    std::lock_guard<std::mutex> lock(_mutex);
    UpdateStatus s = _status;
    if (s.checked) s.checked_age_s = (millis() - _checked_at) / 1000;
    return s;
}

bool UpdateService::busy() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _want_install || _status.phase == UpdatePhase::Downloading || _status.phase == UpdatePhase::Restarting;
}

UpdateService::Request UpdateService::requestCheck() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_want_check || _want_install || _status.phase == UpdatePhase::Checking ||
        _status.phase == UpdatePhase::Downloading || _status.phase == UpdatePhase::Restarting) {
        return Request::Busy;
    }
    uint32_t now = millis();
    if (!_schedule.manualAllowed(now)) return Request::TooSoon;
    _schedule.manualStarted(now);
    _want_check = true;
    return Request::Accepted;
}

UpdateService::Request UpdateService::requestInstall() {
    std::lock_guard<std::mutex> lock(_mutex);
    if (_want_check || _want_install || _status.phase == UpdatePhase::Checking ||
        _status.phase == UpdatePhase::Downloading || _status.phase == UpdatePhase::Restarting) {
        return Request::Busy;
    }
    if (!updateInstallable(_status)) return Request::NotInstallable;
    _want_install = true;
    return Request::Accepted;
}

bool UpdateService::setAutoCheck(bool enabled) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) return false;
    bool ok = prefs.putBool(kKeyAuto, enabled) > 0;
    prefs.end();
    if (!ok) return false;
    std::lock_guard<std::mutex> lock(_mutex);
    _status.auto_check = enabled;
    _schedule.setAuto(enabled);
    return true;
}

bool UpdateService::networkUp() const {
    return _network && _network->isConnected() && !_network->isApMode();
}

bool UpdateService::takeRestartRequest() {
    std::lock_guard<std::mutex> lock(_mutex);
    bool wanted = _restart_request;
    _restart_request = false;
    return wanted;
}

void UpdateService::setPhase(UpdatePhase phase, const String& error, uint8_t progress) {
    std::lock_guard<std::mutex> lock(_mutex);
    _status.phase = phase;
    _status.error = error;
    _status.progress = progress;
}

void UpdateService::taskEntry(void* arg) {
    static_cast<UpdateService*>(arg)->run();
}

void UpdateService::run() {
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        bool manual = false, install_now = false, auto_due = false;
        bool net = networkUp();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_status.phase == UpdatePhase::Restarting) continue;
            manual = _want_check;
            install_now = _want_install;
            auto_due = _schedule.due(millis(), net);
        }
        if (install_now) {
            install();
        } else if (manual || auto_due) {
            check(manual);
        }
    }
}

void UpdateService::check(bool manual) {
    setPhase(UpdatePhase::Checking);
    String error;
    ReleaseInfo info;
    bool ok = false;

    if (!networkUp()) {
        error = "No network connection";
    } else {
        String url = String("https://api.github.com/repos/") + NUT_UPDATE_REPO + "/releases/latest";
        HttpsStream http;
        int code = http.open(url.c_str(), "application/vnd.github+json", userAgent().c_str());
        if (code == 200) {
            JsonDocument filter;
            releaseJsonFilter(filter);
            JsonDocument doc;
            DeserializationError jerr = deserializeJson(doc, http, DeserializationOption::Filter(filter));
            if (jerr) {
                error = String("Invalid release data: ") + jerr.c_str();
            } else {
                ok = parseRelease(doc, kReleaseSuffix, info, error);
            }
        } else if (code == 404) {
            error = "No release found";
        } else if (code == 403 || code == 429) {
            error = "GitHub rate limit reached, try again later";
        } else if (code < 0) {
            error = http.error();
        } else {
            error = String("GitHub answered HTTP ") + code;
        }
    }

    uint32_t now = millis();
    bool log_available = false, log_failure = false, log_recovered = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _want_check = false;
        _schedule.checked(now, ok);
        _checked_at = now;
        _status.checked = true;
        _status.last_check_ok = ok;
        _status.phase = UpdatePhase::Idle;
        _status.progress = 0;
        _status.error = ok ? String() : error;
        if (ok) {
            _status.latest = info.tag;
            _status.release_url = info.page_url;
            _status.asset_name = info.has_asset ? info.asset.name : String();
            _status.asset_size = info.has_asset ? info.asset.size : 0;
            _status.asset_verified = info.has_asset && info.asset.sha256.length() == 64;
            _asset = info.asset;
            if (updateAvailable(_status.current, info.tag) && _logged_latest != info.tag) {
                _logged_latest = info.tag;
                log_available = true;
            }
            log_recovered = _check_failing;
            _check_failing = false;
        } else {
            log_failure = manual || !_check_failing;
            _check_failing = true;
        }
    }
    if (log_available) {
        AppLogger::log("INFO", "[UPDATE] Firmware %s is available (running %s)", info.tag.c_str(), FIRMWARE_VERSION);
    } else if (ok && (manual || log_recovered)) {
        AppLogger::log("INFO", "[UPDATE] Update check done: latest release %s", info.tag.c_str());
    }
    if (ok && info.tag.length() > 0 && !info.has_asset && manual) {
        AppLogger::log("WARN", "[UPDATE] Release %s has no firmware file for this build (%s)", info.tag.c_str(),
                       NUT_BUILD_ENV);
    }
    if (log_failure) AppLogger::log("WARN", "[UPDATE] Update check failed: %s", error.c_str());
}

namespace {

String toHex(const uint8_t* d, size_t n) {
    static const char* hex = "0123456789abcdef";
    String s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) {
        s += hex[d[i] >> 4];
        s += hex[d[i] & 0x0F];
    }
    return s;
}

}  // namespace

void UpdateService::failInstall(const String& error) {
    if (Update.isRunning()) Update.abort();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _want_install = false;
        _status.phase = UpdatePhase::Failed;
        _status.error = error;
        _status.progress = 0;
    }
    AppLogger::log("ERROR", "[UPDATE] Install failed: %s", error.c_str());
}

void UpdateService::install() {
    ReleaseAsset asset;
    String tag;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        asset = _asset;
        tag = _status.latest;
        _status.phase = UpdatePhase::Downloading;
        _status.error = String();
        _status.progress = 0;
    }
    AppLogger::log("INFO", "[UPDATE] Installing %s from %s", tag.c_str(), asset.name.c_str());

    if (Update.isRunning()) return failInstall("Another firmware upload is in progress");
    if (!networkUp()) return failInstall("No network connection");
    if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < MIN_FREE_HEAP ||
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < MIN_FREE_BLOCK) {
        return failInstall("Not enough free memory");
    }

    HttpsStream http;
    int code = http.open(asset.url.c_str(), "application/octet-stream", userAgent().c_str());
    if (code != 200) return failInstall(code < 0 ? http.error() : String("Download answered HTTP ") + code);
    if (http.contentLength() >= 0 && (uint64_t)http.contentLength() != asset.size) {
        return failInstall("Download size does not match the release");
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);

    static uint8_t buf[4096];
    uint32_t total = 0;
    bool started = false;
    uint8_t last_pct = 255;
    String error;
    for (;;) {
        int n = http.readChunk(buf, sizeof(buf));
        if (n < 0) {
            error = http.error();
            break;
        }
        if (n == 0) break;
        total += (uint32_t)n;
        if (total > asset.size) {
            error = "Download larger than the release";
            break;
        }
        mbedtls_sha256_update(&sha, buf, (size_t)n);
        if (!started) {
            if (buf[0] != 0xE9) {
                error = "Not a firmware image";
                break;
            }
            if (!Update.begin(asset.size, U_FLASH)) {
                error = String("Firmware image rejected: ") + Update.errorString();
                break;
            }
            started = true;
        }
        if (Update.write(buf, (size_t)n) != (size_t)n) {
            error = String("Flash write failed: ") + Update.errorString();
            break;
        }
        uint8_t pct = asset.size ? (uint8_t)((uint64_t)total * 100 / asset.size) : 0;
        if (pct != last_pct) {
            last_pct = pct;
            std::lock_guard<std::mutex> lock(_mutex);
            _status.progress = pct;
        }
    }
    uint8_t digest[32];
    mbedtls_sha256_finish(&sha, digest);
    mbedtls_sha256_free(&sha);
    http.close();

    if (error.length() > 0) return failInstall(error);
    if (total != asset.size) return failInstall("Download incomplete");
    if (toHex(digest, sizeof(digest)) != asset.sha256) return failInstall("SHA-256 does not match the release");
    if (!started || !Update.end(true)) {
        return failInstall(String("Firmware image rejected: ") + Update.errorString());
    }

    AppLogger::log("INFO", "[UPDATE] Installed %s (%u bytes), restarting", tag.c_str(), (unsigned)total);
    std::lock_guard<std::mutex> lock(_mutex);
    _status.phase = UpdatePhase::Restarting;
    _status.error = String();
    _status.progress = 100;
    _want_install = false;
    _restart_request = true;
}
