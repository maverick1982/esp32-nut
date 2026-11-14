#ifndef CORE_UPDATE_SERVICE_H
#define CORE_UPDATE_SERVICE_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mutex>
#include "ReleaseInfo.h"
#include "UpdateSchedule.h"
#include "UpdateStatus.h"

class AppNetworkManager;

// Checks GitHub for a newer release daily or on request, and installs this build's
// release zip from GitHub. Checks and installs run in its own task.
class UpdateService {
public:
    enum class Request : uint8_t { Accepted, Busy, TooSoon, NotInstallable };

    void begin(const AppNetworkManager* network);

    UpdateStatus status() const;
    bool busy() const;

    Request requestCheck();
    Request requestInstall();
    bool setAutoCheck(bool enabled);
    // true once after a successful install
    bool takeRestartRequest();

private:
    static void taskEntry(void* arg);
    void run();
    bool networkUp() const;
    void check(bool manual);
    void install();
    void setPhase(UpdatePhase phase, const String& error = String(), uint8_t progress = 0);
    void failInstall(const String& error);

    const AppNetworkManager* _network = nullptr;
    TaskHandle_t _task = nullptr;

    mutable std::mutex _mutex;
    UpdateStatus _status;
    UpdateSchedule _schedule;
    ReleaseAsset _asset;
    uint32_t _checked_at = 0;
    bool _want_check = false;
    bool _want_install = false;
    bool _restart_request = false;
    String _logged_latest;
    bool _check_failing = false;
};

#endif // CORE_UPDATE_SERVICE_H
