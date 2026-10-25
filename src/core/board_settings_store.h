#ifndef CORE_BOARD_SETTINGS_STORE_H
#define CORE_BOARD_SETTINGS_STORE_H

#include "board/board_settings.h"

/**
 * Board settings of the web UI (US-094), kept in the NVS namespace "board": led_on,
 * led_level, usb_port. load() gives the profile defaults for missing keys and
 * sanitizes the stored values (PDR 0002 D4).
 */
class BoardSettingsStore {
public:
    BoardSettings load(const BoardConfig& board);
    bool save(const BoardSettings& settings);
};

#endif // CORE_BOARD_SETTINGS_STORE_H
