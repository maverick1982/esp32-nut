#ifndef UPDATE_STATUS_H
#define UPDATE_STATUS_H

#include <Arduino.h>
#include "FirmwareVersion.h"

enum class UpdatePhase : uint8_t { Idle, Checking, Downloading, Restarting, Failed };

inline const char* updatePhaseName(UpdatePhase p) {
    switch (p) {
    case UpdatePhase::Checking: return "checking";
    case UpdatePhase::Downloading: return "downloading";
    case UpdatePhase::Restarting: return "restarting";
    case UpdatePhase::Failed: return "failed";
    default: return "idle";
    }
}

struct UpdateStatus {
    String current;
    String latest;        // empty until a check succeeds
    String release_url;
    String asset_name;    // empty when the release has no zip for this build
    uint32_t asset_size;
    bool asset_verified;  // the release publishes a SHA-256 digest for the zip
    bool checked;
    bool last_check_ok;
    uint32_t checked_age_s;
    String error;
    UpdatePhase phase;
    uint8_t progress;     // percent while downloading
    bool auto_check;
};

// The latest release is newer than this build
inline bool updateAvailable(const String& current, const String& latest) {
    FirmwareVersion c, l;
    if (!parseFirmwareVersion(current.c_str(), c) || !parseFirmwareVersion(latest.c_str(), l)) return false;
    return compareFirmwareVersions(l, c) > 0;
}

// The latest release can be installed on this build
inline bool updateInstallable(const UpdateStatus& s) {
    if (s.latest.length() == 0 || s.asset_name.length() == 0 || !s.asset_verified) return false;
    FirmwareVersion c;
    if (!parseFirmwareVersion(s.current.c_str(), c)) return true;
    return updateAvailable(s.current, s.latest);
}

#endif // UPDATE_STATUS_H
