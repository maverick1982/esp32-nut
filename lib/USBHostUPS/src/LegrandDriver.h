/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/legrand-hid.c:
 *   Copyright (C) 2003 - 2012 Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2005 - 2006 Peter Selinger <selinger@users.sourceforge.net>
 *                 2008 - 2009 Arjen de Korte <adkorte-guest@alioth.debian.org>
 *                 2013 Charles Lepple <clepple+nut@gmail.com>
 *                 2018 Gabriele Taormina <gabriele.taormina@legrand.com>, for Legrand
 */

#ifndef LEGRAND_DRIVER_H
#define LEGRAND_DRIVER_H

#include "GenericDriver.h"
#include <Arduino.h>

// Legrand Keor PDU (1cb0:0038) and Keor SP (1cb0:0032), after legrand-hid.c (US-087, US-106)
class LegrandDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "LegrandDriver"; }

    // legrand-hid.c: beeper.enable writes 2 and beeper.disable writes 3 on
    // UPS.PowerSummary.AudibleAlarmControl. A 1-bit field keeps the on/off encoding.
    uint8_t encodeBeeperValue(bool enable, uint16_t bit_size) const override {
        if (bit_size == 1) return enable ? 1 : 0;
        return enable ? 2 : 3;
    }

    // legrand-hid.c has no beeper.mute, and 3 means disabled here
    bool beeperMuteSupported() const override { return false; }

    // ups.delay.* rows of legrand_hid2nut (legrand-hid.c:165-168): applyDefaultDelays() sets
    // them, legrandHid2Nut() cites them as excluded (US-066)
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::legrandDelayPaths(); }

    // Rows of legrand_hid2nut (legrand-hid.c), NutTable.h (US-106)
    const NutTable::HidInfo* hid2nut(size_t& n) const override { return NutTable::legrandHid2Nut(n); }
    // genericHid2Nut() rows whose keys come from legrandHid2Nut() (US-106)
    const char* const* supersededGenericPaths(size_t& n) const override;
    // Instant commands from the HU_TYPE_CMD rows of legrand_hid2nut (US-106)
    const NutTable::HidInfo* commandTable(size_t& n) const override { return NutTable::legrandHid2Nut(n); }
    // legrand_format_mfr: "Legrand" when the device has no manufacturer string (US-106)
    void formatDeviceStrings(UPSData& data) override;

    LegrandDriver() = default;
    virtual ~LegrandDriver() = default;

protected:
    // ups.beeper.status with the Legrand meaning of the field: not a row of legrand_hid2nut
    void decodeBeeperStatus(UPSData& data, double v, const HIDUsageDef& def) override;
};

#endif // LEGRAND_DRIVER_H
