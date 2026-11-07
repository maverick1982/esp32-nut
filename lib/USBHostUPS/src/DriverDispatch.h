#ifndef DRIVER_DISPATCH_H
#define DRIVER_DISPATCH_H

#include <Arduino.h>
#include <stddef.h>
#include <utility>
#include <vector>
#include "IHidDriver.h"
#include "NutTable.h"
#include "UPSData.h"

/**
 * @brief The host's only way to the HID hooks of the attached driver (US-131, ADR 0013
 * section 1): the one place of the host where IUPSDriver::hid() is called.
 *
 * Every function accepts a null driver. For a driver without the HID extension (hid() ==
 * nullptr) nothing HID is called and the neutral value is returned: no report requested, no
 * command row, the INPUT watchdog left inert. For an HID driver each call reaches the same
 * method with the same arguments as before. No lock is taken: the callers keep theirs.
 */
namespace DriverDispatch {

inline IHidDriver* hidOf(IUPSDriver* d) { return d ? d->hid() : nullptr; }
inline const IHidDriver* hidOf(const IUPSDriver* d) { return d ? d->hid() : nullptr; }

// Right after the driver is created: the HID transport poll() runs on
inline void attach(IUPSDriver* d, IHidTransport* transport) {
    if (IHidDriver* h = hidOf(d)) h->bindTransport(transport);
}

// A received report: decodeReport(), then the NUT table writes its variables last (US-066)
inline void decode(IUPSDriver* d, IHidTransport* t, uint8_t report_id, uint8_t report_type,
                   const uint8_t* data, size_t length, UPSData& ups) {
    IHidDriver* h = hidOf(d);
    if (!h) return;
    h->decodeReport(t, report_id, report_type, data, length, ups);
    h->postDecode(t, report_id, report_type, data, length, ups);
}

inline void parseString(IUPSDriver* d, IHidTransport* t, uint8_t index, const uint8_t* data,
                        size_t length, UPSData& ups) {
    if (IHidDriver* h = hidOf(d)) h->parseStringDescriptor(t, index, data, length, ups);
}

// For InputWatchdog::setValuesPolled(): true without HID, so the watchdog never makes the
// data stale nor asks for a recovery or a restart (US-123 semantics)
inline bool inputValuesPolled(const IUPSDriver* d) {
    const IHidDriver* h = hidOf(d);
    return h ? h->inputValuesPolled() : true;
}

// The INPUT watchdog guards the values, for the diagnostics export
inline bool inputWatchdogActive(const IUPSDriver* d, bool no_interrupt_pipe) {
    const IHidDriver* h = hidOf(d);
    return h && !no_interrupt_pipe && !h->inputValuesPolled();
}

inline uint16_t requestLength(const IUPSDriver* d, uint8_t report_type, uint8_t report_id, uint16_t expected) {
    const IHidDriver* h = hidOf(d);
    return h ? h->requestLength(report_type, report_id, expected) : expected;
}

// Without HID the on-demand read of the export requests nothing
inline bool acceptDiagReport(const IUPSDriver* d, uint8_t report_type, uint8_t report_id) {
    const IHidDriver* h = hidOf(d);
    return h ? h->acceptDiagReport(report_type, report_id) : false;
}

inline void invalidateReport(IUPSDriver* d, uint8_t report_type, uint8_t report_id) {
    if (IHidDriver* h = hidOf(d)) h->invalidateReport(report_type, report_id);
}

inline NutTable::SetStatus planSet(const IUPSDriver* d, const UPSData& data, const std::vector<HIDUsageDef>& usages,
                                   const char* name, const char* value, NutTable::SetPlan& plan) {
    const IHidDriver* h = hidOf(d);
    return h ? h->planSet(data, usages, name, value, plan) : NutTable::SetStatus::UNKNOWN;
}

inline const NutTable::HidInfo* commandTable(const IUPSDriver* d, size_t& n) {
    n = 0;
    const IHidDriver* h = hidOf(d);
    return h ? h->commandTable(n) : nullptr;
}

inline bool beeperFieldRead(const IUPSDriver* d, const IHidTransport* t) {
    const IHidDriver* h = hidOf(d);
    return h && h->beeperFieldRead(t);
}

// False only for an HID driver that ignores the beeper commands (EcoFlow), as before
inline bool beeperControllable(const IUPSDriver* d) {
    const IHidDriver* h = hidOf(d);
    return h ? h->beeperControllable() : true;
}

inline bool beeperMuteSupported(const IUPSDriver* d) {
    const IHidDriver* h = hidOf(d);
    return h ? h->beeperMuteSupported() : true;
}

/**
 * ups.delay.* at connection (US-058): for an HID driver on the paths of its subdriver, as
 * before; without HID both variables when the driver declares them, with the same defaults
 * and saved values.
 */
inline void applyDefaultDelays(const IUPSDriver* d, UPSData& data, const std::vector<HIDUsageDef>& usages,
                               const std::vector<std::pair<String, String>>* saved) {
    if (!d) return;
    if (const IHidDriver* h = d->hid()) {
        NutTable::applyDefaultDelays(data, usages, h->defaultOffDelay(), h->defaultOnDelay(),
                                     h->delayVariablesSupported(), h->delayPaths(), saved);
        return;
    }
    bool vars = d->delayVariablesSupported();
    NutTable::applyDelayValues(data, d->defaultOffDelay(), d->defaultOnDelay(), vars, vars, saved);
}

} // namespace DriverDispatch

#endif // DRIVER_DISPATCH_H
