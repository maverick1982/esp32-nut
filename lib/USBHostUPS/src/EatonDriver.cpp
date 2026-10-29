/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/mge-hid.c:
 *   Copyright (C) 2003 - 2015 Arnaud Quette <arnaud.quette@free.fr>
 *                 2015 - 2024 Eaton / Arnaud Quette <ArnaudQuette@Eaton.com>
 *                 2020 - 2026 Jim Klimov <jimklimov+nut@gmail.com>
 *                 2024 - 2025 "DaRK AnGeL" <28630321+masterwishx@users.noreply.github.com>
 */

#include "EatonDriver.h"
#include "IHidTransport.h"
#include "HIDParser.h"
#include "HIDUsages.h"

/**
 * @brief Eaton (MGE) Driver Implementation
 * 
 * ADR 0003 COMPLIANCE:
 * This sub-driver faithfully mirrors the official NUT behavior for Eaton/MGE HID devices.
 * - Reference: nut_repo/drivers/mge-hid.c
 * - Quirk 254/255: Mirrored from nut_repo/drivers/libhid.c (skip reports 254/255 to prevent freeze).
 * - Every variable, the status and the instant commands come from mgeHid2Nut() (US-073,
 *   US-074, ADR 0011): the standard mappings of the same keys are superseded below, and the
 *   driver keeps only hooks (string descriptors, model and country context, quirks).
 */

EatonDriver::EatonDriver() : _iModelIdx(0), _iModelIdxSeen(false), _iModelSeen(false),
    _apparentPowerSeen(false), _apparentPower(0) {}

void EatonDriver::setup() {
    GenericDriver::setup();
    _map.invalidate();
    _iModelIdx = 0;
    _iModelIdxSeen = false;
    _iModelSeen = false;
    _iModel = "";
    _apparentPowerSeen = false;
    _apparentPower = 0;
    // mge_type and country_code are known before the first walk upstream: the
    // model-dependent rows wait for them (NutTable::DecodeResult::DEFER)
    _nut_ctx.ready = false;
}

static bool hasUsagePath(const std::vector<HIDUsageDef>& usages, const char* path) {
    for (const auto& u : usages) {
        if (NutTable::pathMatches(u, path)) return true;
    }
    return false;
}

// mge-hid.c usage codes, unnamed in NUTUsages.h
static const char* const kIModelPath = "UPS.PowerSummary.0xFFFF00F0";        // iModel
static const char* const kCountryPath = "UPS.PowerSummary.0xFFFF0095";       // Country
static const char* const kApparentPowerPath = "UPS.Flow.[4].ConfigApparentPower";

void EatonDriver::updateNutContext(IHidTransport* host, const UPSData& data) {
    if (_nut_ctx.ready) return;
    const auto& usages = host->getUsages();

    // mge_format_model(): iModel, else the Flow.[4] apparent power, else no model. An
    // iModel string that never arrives counts as empty, as a failed HIDGetItemString().
    String model = _iModel;
    if (model.length() == 0 && _apparentPowerSeen) model = String(_apparentPower);
    bool model_known;
    if (hasUsagePath(usages, kIModelPath) && (!_iModelIdxSeen || (_iModelIdx > 0 && !_iModelSeen))) {
        model_known = false;
    } else if (_iModel.length() > 0) {
        model_known = true;
    } else if (hasUsagePath(usages, kApparentPowerPath)) {
        model_known = _apparentPowerSeen;
    } else {
        model_known = true;
    }
    // device.country, an internal row of mgeHid2Nut() (eaton_check_country_info)
    bool country_known = !hasUsagePath(usages, kCountryPath) || _nut_ctx.country_seen;
    bool product_known = data.hasKey("ups.model") || host->_iProduct == 0;

    // The first full poll is where upstream's initinfo would be done: decide with what is known
    if ((model_known && country_known && product_known) || initialPollDone()) {
        String product = data.get("ups.model", "unknown");
        _nut_ctx.mge_type = NutTable::mgeModelType(product.c_str(), model.c_str());
        _nut_ctx.ready = true;
    }
}

// The standard mappings of GenericDriver::decodeReport() whose keys come from the US-073
// rows of mgeHid2Nut(), copied character by character from that table
static const char* const kSupersededGenericPaths[] = {
    // Status: only the BOOL rows of mge_hid2nut set the internal ups.status.* keys
    "UPS.PowerSummary.PresentStatus.ACPresent", "UPS.PowerSummary.ACPresent",
    "UPS.PowerSummary.PresentStatus.Discharging", "UPS.PowerSummary.Discharging",
    "UPS.PowerSummary.PresentStatus.Charging", "UPS.PowerSummary.Charging",
    "UPS.PowerSummary.PresentStatus.BelowRemainingCapacityLimit", "UPS.PowerSummary.BelowRemainingCapacityLimit",
    "UPS.PowerSummary.PresentStatus.NeedReplacement", "UPS.PowerSummary.NeedReplacement",
    "UPS.PowerSummary.PresentStatus.Overload", "UPS.PowerSummary.Overload",
    "UPS.PowerSummary.PresentStatus.ShutdownImminent", "UPS.PowerSummary.ShutdownImminent",
    "UPS.PowerSummary.PresentStatus.CommunicationLost", "UPS.PowerSummary.CommunicationLost",
    "UPS.PowerSummary.PresentStatus.BatteryPresent", "UPS.PowerSummary.PresentStatus.RemainingTimeLimitExpired",
    "UPS.PowerSummary.PresentStatus.FullyCharged", "UPS.PowerSummary.PresentStatus.FullyDischarged",
    // battery.voltage
    "UPS.PowerSummary.Voltage", "UPS.BatterySystem.Voltage", "UPS.Battery.Voltage",
    // battery.charge, battery.charge.low
    "UPS.PowerSummary.RemainingCapacity", "UPS.PowerSummary.RemainingCapacityLimit",
    // battery.runtime
    "UPS.PowerSummary.RunTimeToEmpty", "UPS.Battery.RunTimeToEmpty",
    // ups.load
    "UPS.PowerSummary.PercentLoad", "UPS.PowerConverter.Output.PercentLoad", "UPS.Output.PercentLoad",
    // ups.temperature, battery.temperature
    "UPS.PowerSummary.Temperature", "UPS.Battery.Temperature",
    // battery.capacity
    "UPS.BatterySystem.Battery.DesignCapacity",
    // battery.capacity.full: ADR 0011 rule 2, not in nut-names.txt
    "UPS.BatterySystem.Battery.FullChargeCapacity",
    // battery.voltage.nominal
    "UPS.PowerSummary.ConfigVoltage", "UPS.Battery.ConfigVoltage",
    // ups.test.result
    "UPS.BatterySystem.Battery.Test", "UPS.Battery.Test", "UPS.Output.Test",
    // ups.timer.shutdown
    "UPS.PowerSummary.DelayBeforeShutdown",
    // US-074. input.voltage, input.frequency
    "UPS.PowerConverter.Input.Voltage", "UPS.Input.Voltage", "UPS.Flow.Voltage",
    "UPS.PowerConverter.Input.Frequency", "UPS.Input.Frequency", "UPS.Flow.Frequency",
    // output.voltage, output.frequency
    "UPS.PowerConverter.Output.Voltage", "UPS.Output.Voltage",
    "UPS.PowerConverter.Output.Frequency", "UPS.Output.Frequency",
    // ups.realpower, ups.power
    "UPS.PowerConverter.Output.ActivePower", "UPS.Output.ActivePower",
    "UPS.PowerConverter.Output.ApparentPower", "UPS.Output.ApparentPower",
    // ups.realpower.nominal, ups.power.nominal
    "UPS.Flow.ConfigActivePower", "UPS.PowerConverter.ConfigActivePower", "UPS.Output.ConfigActivePower",
    "UPS.Flow.ConfigApparentPower",
    // input.voltage.nominal: ADR 0011 rule 2, the Flow.[4] value is the output nominal
    "UPS.Flow.ConfigVoltage", "UPS.Input.ConfigVoltage",
    // output.voltage.nominal
    "UPS.PowerConverter.Output.ConfigVoltage", "UPS.Output.ConfigVoltage",
    // input.transfer.high, input.transfer.low
    "UPS.PowerConverter.Output.HighVoltageTransfer", "UPS.PowerConverter.Output.LowVoltageTransfer",
    "UPS.Output.LowVoltageTransfer", "UPS.Input.LowVoltageTransfer",
    "UPS.Output.HighVoltageTransfer", "UPS.Input.HighVoltageTransfer",
    // ups.beeper.status
    "UPS.PowerSummary.AudibleAlarmControl", "UPS.BatterySystem.Battery.AudibleAlarmControl", "UPS.AudibleAlarmControl",
    // Kept (ADR 0011 rule 2, standard names mge-hid does not read): ups.mfr.date from
    // ManufacturerDate, battery.mfr.date from Battery.ManufacturerDate and the Date strings
};

const char* const* EatonDriver::supersededGenericPaths(size_t& n) const {
    n = sizeof(kSupersededGenericPaths) / sizeof(kSupersededGenericPaths[0]);
    return kSupersededGenericPaths;
}

void EatonDriver::onLoop(IHidTransport* host, UPSData& data) {
    updateNutContext(host, data);
}

bool EatonDriver::acceptPollReport(uint8_t report_type, uint8_t report_id) const {
    // CRITICAL QUIRK (NUT libhid.c): reports 254/255 freeze or stall Eaton devices
    return report_id != 254 && report_id != 255;
}

void EatonDriver::collectStringRequests(IHidTransport* host, const UPSData& data, std::vector<uint8_t>& out) const {
    GenericDriver::collectStringRequests(host, data, out);
    if (_iModelIdx > 0 && !_iModelSeen) out.push_back(_iModelIdx);
}

void EatonDriver::decodeReport(IHidTransport* host, uint8_t report_id, uint8_t report_type, const uint8_t *data, size_t length, UPSData& ups_data) {
    if (length == 0 || data == NULL || !host) return;

    GenericDriver::decodeReport(host, report_id, report_type, data, length, ups_data);

    typedef UsageMapIndex<EatonDriver>::Mapping Mapping;
    static const Mapping mappings[] = {
        // Context inputs, never published: iModel and Flow.[4].ConfigApparentPower give mge_type
        // (NutTable::Context, US-066); the country is the device.country row
        { kIModelPath, [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef*) { drv->_iModelIdx = (uint8_t)v; drv->_iModelIdxSeen = true; } },
        { "UPS.Flow.ConfigApparentPower", [](EatonDriver* drv, UPSData&, double v, const HIDUsageDef* def) {
            if (def && NutTable::pathMatches(*def, kApparentPowerPath)) {
                drv->_apparentPower = (long)v;
                drv->_apparentPowerSeen = true;
            }
        } }
    };

    _map.apply(this, mappings, host->getUsages(), report_id, report_type, data, length, ups_data);
}

void EatonDriver::parseStringDescriptor(IHidTransport* host, uint8_t index, const uint8_t *data, size_t length, UPSData& ups_data) {
    GenericDriver::parseStringDescriptor(host, index, data, length, ups_data);
    
    if (length < 2 || data[1] != 0x03) return;
    uint8_t str_len = data[0];
    String str = "";
    for (int i = 2; i < str_len && i < length; i += 2) {
        if (data[i] != 0) {
            str += (char)data[i];
        }
    }
    
    if (_iModelIdx > 0 && index == _iModelIdx) {
        _iModel = str;
        _iModelSeen = true;
    }
}
