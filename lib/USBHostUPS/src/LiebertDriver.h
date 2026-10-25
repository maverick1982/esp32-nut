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

// Phoenixtec 06da:ffff (Powerwalker, Salicru...), after liebert-hid.c (US-097)
class LiebertDriver : public GenericDriver {
public:
    const char* getDriverName() const override { return "LiebertDriver"; }

    // ups.delay.* rows of liebert_hid2nut (NutTable.h). They are the only RW rows of the
    // table: hid2nut() is not overridden, applyDefaultDelays() sets them.
    const NutTable::DelayPaths* delayPaths() const override { return NutTable::liebertDelayPaths(); }

    // Instant commands of liebert_hid2nut and those usbhid-ups derives from them
    const char* const* commandAllowList() const override;

    LiebertDriver();
    virtual ~LiebertDriver() = default;

    void setup() override;
    void decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) override;
    void parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) override;

protected:
    void onLoop(IHidTransport* host, UPSData& data) override;
    void collectStringRequests(IHidTransport* host, const UPSData& data, std::vector<uint8_t>& out) const override;

private:
    UsageMapIndex<LiebertDriver> _map;
    uint8_t _chemStrIdx;       // battery.type
    bool _vendorTransfer;      // input.transfer.* comes from the PowerConverter.Output vendor usages
};

#endif // LIEBERT_DRIVER_H
