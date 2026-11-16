#ifndef FIRMWARE_VERSION_COMPARE_H
#define FIRMWARE_VERSION_COMPARE_H

#include <stdint.h>
#include <stdlib.h>

// Release version: "v1.16.0" or "v1.17.0-rc1"
struct FirmwareVersion {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    bool prerelease;
};

inline bool parseFirmwareVersion(const char* s, FirmwareVersion& out) {
    if (!s || *s != 'v') return false;
    s++;
    uint32_t parts[3];
    for (int i = 0; i < 3; i++) {
        if (*s < '0' || *s > '9') return false;
        char* end = nullptr;
        unsigned long v = strtoul(s, &end, 10);
        if (end == s || v > 0xFFFFFFUL) return false;
        parts[i] = (uint32_t)v;
        s = end;
        if (i < 2) {
            if (*s != '.') return false;
            s++;
        }
    }
    bool rc = false;
    if (*s != '\0') {
        if (s[0] != '-' || s[1] != 'r' || s[2] != 'c' || s[3] < '0' || s[3] > '9') return false;
        for (s += 3; *s; s++) {
            if (*s < '0' || *s > '9') return false;
        }
        rc = true;
    }
    out.major = parts[0];
    out.minor = parts[1];
    out.patch = parts[2];
    out.prerelease = rc;
    return true;
}

// <0, 0, >0; a pre-release sorts before the release with the same numbers
inline int compareFirmwareVersions(const FirmwareVersion& a, const FirmwareVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.prerelease != b.prerelease) return a.prerelease ? -1 : 1;
    return 0;
}

#endif // FIRMWARE_VERSION_COMPARE_H
