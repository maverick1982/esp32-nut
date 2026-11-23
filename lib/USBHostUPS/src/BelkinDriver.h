/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/belkin-hid.c:
 *   Copyright (C) 2003 - 2008 Arnaud Quette <arnaud.quette@free.fr>
 *                 2005 Peter Selinger <selinger@users.sourceforge.net>
 *                 2011, 2014 Charles Lepple <clepple+nut@gmail.com>
 *                 2024 James R. Parks <jrjparks@zathera.com>
 *                 2024 - 2026 Jim Klimov <jimklimov+nut@gmail.com>
 */

#ifndef BELKIN_DRIVER_H
#define BELKIN_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

// Belkin (050d) and Liebert / Vertiv (10af:0000-0008) units, after belkin-hid.c
class BelkinDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "BelkinDriver"; }

    // ups.delay.* rows of belkin_hid2nut (belkin-hid.c:496-497): applyDefaultDelays() sets
    // them, belkinHid2Nut() cites them as excluded (US-066)
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::belkinDelayPaths(); }

    // Rows of belkin_hid2nut (belkin-hid.c), NutTable.h (ADR 0011 rule 4)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::belkinHid2Nut(n); }
    // genericHid2Nut() rows whose keys come from belkinHid2Nut()
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of belkin_hid2nut
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::belkinHid2Nut(n); }
    // belkin_format_mfr / belkin_format_model: "Belkin" and "unknown" without the strings
    void formatDeviceStrings(UPSData& data) override;

    BelkinDriver() = default;
    virtual ~BelkinDriver() = default;
};

#endif // BELKIN_DRIVER_H
