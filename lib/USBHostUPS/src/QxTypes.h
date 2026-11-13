/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx.h:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *   Based on usbhid-ups.h - Copyright (C)
 *     2003-2009 Arnaud Quette <http://arnaud.quette.free.fr/contact.html>
 *     2005-2006 Peter Selinger <selinger@users.sourceforge.net>
 *     2007-2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 */

#ifndef QX_TYPES_H
#define QX_TYPES_H

#include <stddef.h>
#include <stdint.h>

/**
 * Types of the Q* engine (ADR 0013, section 4): a port of nut_repo/drivers/nutdrv_qx.h.
 *
 * The protocol tables are static const data. Upstream item_t also carries the runtime
 * fields answer and value: here they live in the QxEngine instance, so a QxItem row has the
 * columns of item_t in the same order without those two (they are always "" in the
 * upstream tables). The flags a walk changes at runtime (QX_FLAG_SKIP, ...) are copied per
 * row into the engine as well.
 */

// SMALLBUF of NUT (include/common.h:780): same buffer sizes, same truncations
static const size_t QX_SMALLBUF = 512;

// Driver's own flags (nutdrv_qx.h)
static const uint32_t QX_FLAG_STATIC = 2UL;            // Retrieve info only once
static const uint32_t QX_FLAG_SEMI_STATIC = 4UL;       // Retrieve info only after a command/setvar
static const uint32_t QX_FLAG_ABSENT = 8UL;            // Data is absent in the device, use default value
static const uint32_t QX_FLAG_QUICK_POLL = 16UL;       // Mandatory vars, polled also in the quick update
static const uint32_t QX_FLAG_CMD = 32UL;              // Instant command
static const uint32_t QX_FLAG_SETVAR = 64UL;           // The var is settable
static const uint32_t QX_FLAG_TRIM = 128UL;            // Trim leading/trailing spaces/hashes
static const uint32_t QX_FLAG_ENUM = 256UL;            // Enum values stored in info_rw
static const uint32_t QX_FLAG_RANGE = 512UL;           // Ranges stored in info_rw
static const uint32_t QX_FLAG_NONUT = 1024UL;          // No corresponding var in NUT
static const uint32_t QX_FLAG_SKIP = 2048UL;           // Skip this var
static const uint32_t QX_FLAG_MAPPING_HANDLED = 4096UL; // Raised when a walk handled the row

// MAXTRIES of nutdrv_qx.h
static const int QX_MAXTRIES = 3;

// ST_FLAG_* of NUT (include/extstate.h:39-40), for info_flags; prefixed so that they do not
// clash with NutTable::ST_FLAG_* where both headers are visible
static const int QX_ST_FLAG_RW = 0x0001;
static const int QX_ST_FLAG_STRING = 0x0002;

// status_bit_t of nutdrv_qx.h, same order
enum QxStatusBit : unsigned int {
    QX_OL = 0,  // On line
    QX_LB,      // Low battery
    QX_RB,      // Replace battery
    QX_CHRG,    // Charging
    QX_DISCHRG, // Discharging
    QX_BYPASS,  // On bypass
    QX_CALIB,   // Calibration
    QX_OFF,     // UPS is off
    QX_OVER,    // Overload
    QX_TRIM,    // SmartTrim
    QX_BOOST,   // SmartBoost
    QX_FSD      // Shutdown imminent
};

// STATUS(x) of nutdrv_qx.h
inline unsigned int QX_STATUS(unsigned int bit) { return 1U << bit; }

class QxEngine;
struct QxItem;

// info_rw_t: value for enum/range (or length for ST_FLAG_STRING), optional preprocess
struct QxInfoRw {
    const char* value;
    int (*preprocess)(char* value, size_t len);
};

// item_t without the runtime columns answer and value
struct QxItem {
    const char* info_type;  // NUT variable name
    int info_flags;         // ST_FLAG_* values
    const QxInfoRw* info_rw; // r/w infos (enum, range, string length)
    const char* command;    // Command sent to the UPS
    size_t answer_len;      // Expected min length of the answer (0: none)
    char leading;           // Expected leading character of the answer (0: none)
    int from;               // Position of the first character of the value in the answer
    int to;                 // Position of the last character (0: the rest of the line)
    const char* dfl;        // Format of the value, or default value (QX_FLAG_ABSENT)
    uint32_t qxflags;       // QX_FLAG_* (copied into the engine at construction)
    // Last chance to preprocess the command: -1 on errors, else 0 or the command length
    int (*preprocess_command)(QxEngine& qx, const QxItem& item, char* command, size_t commandlen);
    // Preprocess the raw answer (qx.answer()): -1 on errors, else the new length
    int (*preprocess_answer)(QxEngine& qx, const QxItem& item, int len);
    // Preprocess the value (qx.value()) into value: -1 on errors, else 0
    int (*preprocess)(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
};

// subdriver_t (makevartable is not ported: options come from QxOptions)
struct QxSubdriver {
    const char* name;              // Protocol name + space + version
    bool (*claim)(QxEngine& qx);   // True when the device is covered by this subdriver
    const QxItem* qx2nut;          // Ended by a row with info_type nullptr, as upstream
    void (*initups)(QxEngine& qx); // Subdriver specific upsdrv_initups
    void (*initinfo)(QxEngine& qx); // Subdriver specific upsdrv_initinfo
    const char* accepted;          // Reply of an accepted instcmd/setvar
    const char* rejected;          // Reply of a rejected query
};

// The ups.conf flags of the blazer protocols (testvar() upstream); novendor becomes a
// board setting with US-138
struct QxOptions {
    bool novendor = false;
    bool norating = false;
    bool ignoresab = false;
};

#endif // QX_TYPES_H
