/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/liebert-hid.c:
 *   Copyright (C) 2003 - 2008 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2007 Charles Lepple <clepple@gmail.com>
 *                 2018 Markus "Links2004" <help.markus+gitk@gmail.com>
 *                 2023 Blaž Zakrajšek <zakrajsek.blaz@gmail.com>
 */

#ifndef LIEBERT_DRIVER_H
#define LIEBERT_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

// Phoenixtec 06da:ffff (Powerwalker, Salicru...), after liebert-hid.c (US-097, US-106)
class LiebertDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "LiebertDriver"; }

    // ups.delay.* rows of liebert_hid2nut (liebert-hid.c:133-134): applyDefaultDelays() sets
    // them, liebertHid2Nut() cites them as excluded (US-066)
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::liebertDelayPaths(); }

    // Rows of liebert_hid2nut (liebert-hid.c), NutTable.h (US-106)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::liebertHid2Nut(n); }
    // genericHid2Nut() rows whose keys come from liebertHid2Nut() (US-106)
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of liebert_hid2nut (US-106)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::liebertHid2Nut(n); }
    // liebert_format_mfr: "Liebert" when the device has no manufacturer string (US-106)
    void formatDeviceStrings(UPSData& data) override;

    LiebertDriver() = default;
    virtual ~LiebertDriver() = default;
};

#endif // LIEBERT_DRIVER_H
