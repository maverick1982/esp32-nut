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

// Legrand Keor PDU (1cb0:0038) and Keor SP (1cb0:0032), after legrand-hid.c (US-087)
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

    // ups.delay.* rows of legrand_hid2nut (NutTable.h). They are the only RW rows of the
    // table: hid2nut() is not overridden, applyDefaultDelays() sets them.
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::legrandDelayPaths(); }

    LegrandDriver() = default;
    virtual ~LegrandDriver() = default;

    void setup() override;
    void decodeReport(IUSBHostUPS* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IUSBHostUPS* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

protected:
    void onLoop(IUSBHostUPS* host, UPSData& data) override;

private:
    UsageMapIndex<LegrandDriver> _map;
};

#endif // LEGRAND_DRIVER_H
