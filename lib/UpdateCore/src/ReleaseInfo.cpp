#include "ReleaseInfo.h"
#include <string.h>

String releaseZipName(const String& tag, const char* zip_suffix) {
    if (!zip_suffix || tag.length() == 0) return String("");
    return String("esp32-nut-") + tag + zip_suffix + ".zip";
}

void releaseJsonFilter(JsonDocument& filter) {
    filter["tag_name"] = true;
    filter["html_url"] = true;
    filter["draft"] = true;
    filter["prerelease"] = true;
    JsonObject asset = filter["assets"].add<JsonObject>();
    asset["name"] = true;
    asset["size"] = true;
    asset["digest"] = true;
    asset["browser_download_url"] = true;
}

static bool isHexDigest(const char* s) {
    if (!s || strlen(s) != 64) return false;
    for (const char* p = s; *p; p++) {
        bool hex = (*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F');
        if (!hex) return false;
    }
    return true;
}

static String lower(const char* s) {
    String out(s);
    out.toLowerCase();
    return out;
}

static bool isGithubUrl(const char* url) {
    return url && strncmp(url, "https://github.com/", 19) == 0;
}

bool parseRelease(JsonVariantConst release, const char* zip_suffix, ReleaseInfo& out, String& error) {
    out = ReleaseInfo();
    out.has_asset = false;
    const char* tag = release["tag_name"] | (const char*)nullptr;
    if (!tag || !*tag) {
        error = "No release found";
        return false;
    }
    if (release["draft"] | false) {
        error = "Latest release is a draft";
        return false;
    }
    out.tag = tag;
    out.page_url = release["html_url"] | "";

    String wanted = releaseZipName(out.tag, zip_suffix);
    if (wanted.length() == 0) return true;
    for (JsonVariantConst a : release["assets"].as<JsonArrayConst>()) {
        const char* name = a["name"] | "";
        if (wanted != name) continue;
        const char* url = a["browser_download_url"] | "";
        if (!isGithubUrl(url)) {
            error = "Unexpected download address for " + wanted;
            return false;
        }
        out.has_asset = true;
        out.asset.name = name;
        out.asset.url = url;
        out.asset.size = a["size"] | 0u;
        const char* digest = a["digest"] | "";
        if (strncmp(digest, "sha256:", 7) == 0 && isHexDigest(digest + 7)) out.asset.sha256 = lower(digest + 7);
        break;
    }
    return true;
}
