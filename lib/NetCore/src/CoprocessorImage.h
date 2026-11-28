#ifndef COPROCESSOR_IMAGE_H
#define COPROCESSOR_IMAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Check of a Wi-Fi coprocessor image before anything is written (ADR 0015, section 5).
 *
 * Pure logic on the first bytes of the uploaded file. An ESP application image starts with
 * esp_image_header_t (magic 0xE9, chip_id at byte 12) and, 32 bytes in, the application
 * description esp_app_desc_t (magic 0xABCD5432, version at 48, project_name at 80, idf_ver
 * at 144). The ESP-Hosted slave firmware is the project "network_adapter": any other
 * application on the coprocessor would take the Wi-Fi away, so it is refused.
 */
namespace CoprocessorImage {

static const size_t HEAD_LEN = 256;  // what the check needs from the start of the file
static const uint8_t IMAGE_MAGIC = 0xE9;
static const uint32_t APP_DESC_MAGIC = 0xABCD5432;
static const char* const ESP_HOSTED_PROJECT = "network_adapter";

enum class Result : uint8_t { OK, TOO_SHORT, NOT_AN_IMAGE, WRONG_CHIP, NO_APP_DESC, NOT_ESP_HOSTED, UNKNOWN_TARGET };

struct Info {
    uint16_t chip_id = 0;
    char version[33] = "";
    char project[33] = "";
    char idf[33] = "";
};

// Image chip ID (esp_chip_id_t) of the targets ESP-Hosted runs on as a coprocessor;
// 0xFFFF for a target this check does not know
inline uint16_t chipIdForTarget(const char* target) {
    if (!target) return 0xFFFF;
    if (strcmp(target, "esp32c6") == 0) return 0x000D;
    if (strcmp(target, "esp32c5") == 0) return 0x0017;
    if (strcmp(target, "esp32c3") == 0) return 0x0005;
    if (strcmp(target, "esp32c2") == 0) return 0x000C;
    if (strcmp(target, "esp32") == 0) return 0x0000;
    return 0xFFFF;
}

inline void copyField(char (&dst)[33], const uint8_t* src) {
    size_t n = 0;
    while (n < 32 && src[n] != 0) {
        dst[n] = (src[n] >= 0x20 && src[n] < 0x7F) ? (char)src[n] : '?';
        n++;
    }
    dst[n] = '\0';
}

/**
 * @param head the first bytes of the file, at least HEAD_LEN unless the file is shorter
 * @param target the coprocessor as it reports itself ("esp32c6")
 */
inline Result check(const uint8_t* head, size_t len, const char* target, Info& info) {
    info = Info();
    if (!head || len < HEAD_LEN) return Result::TOO_SHORT;
    if (head[0] != IMAGE_MAGIC) return Result::NOT_AN_IMAGE;
    info.chip_id = (uint16_t)(head[12] | (head[13] << 8));
    const uint16_t expected = chipIdForTarget(target);
    if (expected == 0xFFFF) return Result::UNKNOWN_TARGET;
    if (info.chip_id != expected) return Result::WRONG_CHIP;
    const uint32_t magic = (uint32_t)head[32] | ((uint32_t)head[33] << 8) | ((uint32_t)head[34] << 16) |
                           ((uint32_t)head[35] << 24);
    if (magic != APP_DESC_MAGIC) return Result::NO_APP_DESC;
    copyField(info.version, head + 48);
    copyField(info.project, head + 80);
    copyField(info.idf, head + 144);
    if (strcmp(info.project, ESP_HOSTED_PROJECT) != 0) return Result::NOT_ESP_HOSTED;
    return Result::OK;
}

// Message for the Web UI and the System Log
inline void message(Result r, const Info& info, const char* target, char* out, size_t out_size) {
    switch (r) {
        case Result::OK:
            snprintf(out, out_size, "ESP-Hosted firmware %s for %s", info.version, target ? target : "?");
            break;
        case Result::TOO_SHORT:
            snprintf(out, out_size, "The file is too short to be a firmware image");
            break;
        case Result::NOT_AN_IMAGE:
            snprintf(out, out_size, "Not an ESP firmware image");
            break;
        case Result::WRONG_CHIP:
            snprintf(out, out_size, "The image is for another chip (chip ID 0x%04X), the coprocessor is an %s",
                     (unsigned)info.chip_id, target ? target : "?");
            break;
        case Result::NO_APP_DESC:
            snprintf(out, out_size, "Not an application image (no application description)");
            break;
        case Result::NOT_ESP_HOSTED:
            snprintf(out, out_size, "Not an ESP-Hosted coprocessor firmware (project \"%s\")", info.project);
            break;
        case Result::UNKNOWN_TARGET:
        default:
            snprintf(out, out_size, "Unknown coprocessor \"%s\"", target ? target : "");
            break;
    }
}

}  // namespace CoprocessorImage

#endif  // COPROCESSOR_IMAGE_H
