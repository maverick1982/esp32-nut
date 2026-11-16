#ifndef RELEASE_INFO_H
#define RELEASE_INFO_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Release firmware.bin of this build in a GitHub release
struct ReleaseAsset {
    String name;
    String url;
    String sha256;  // 64 lowercase hex digits, empty when the release has no digest
    uint32_t size;
};

struct ReleaseInfo {
    String tag;
    String page_url;
    bool has_asset;
    ReleaseAsset asset;
};

// "esp32-nut-<tag><suffix>-ota.bin"; empty when suffix is null
String releaseAssetName(const String& tag, const char* suffix);

// Fields of GET /repos/{owner}/{repo}/releases/latest kept by parseRelease()
void releaseJsonFilter(JsonDocument& filter);

// false with error set when the release JSON is unusable
bool parseRelease(JsonVariantConst release, const char* suffix, ReleaseInfo& out, String& error);

#endif // RELEASE_INFO_H
