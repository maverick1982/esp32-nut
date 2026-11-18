#ifndef QX_DESYNC_POLICY_H
#define QX_DESYNC_POLICY_H

#include <stdint.h>
#include "IQxUsbPrimitives.h"

/**
 * @brief What the host does with QX_USB_ERR_DESYNC (US-141): the QX_USB_ERROR_DESYNC case of
 * qx_command(), nut_repo/drivers/nutdrv_qx.c:4463-4472 (NUT PR #3720).
 *
 * Pure logic, so it runs in the native unit tests, like PortCyclePolicy.
 *
 * NUT resets the USB device and reconnects, up to QX_USB_DESYNC_RESET_TRIES (3) times in a
 * row; one more DESYNC ends the driver (fatalx "Replies from the UPS stayed out of step with
 * commands after 3 USB device resets in a row"). Any command that returns >= 0 ends the
 * streak. Here the reset is a power cycle of the root port (the UPS re-enumerates, as after
 * usb_reset() and the reopen), and the end of the driver is a board restart. The streak
 * survives the re-enumeration, as the static counter of NUT survives the reconnection.
 */
class QxDesyncPolicy {
public:
    enum class Action : uint8_t { NONE, RESET_DEVICE, RESTART };

    static const uint8_t RESET_TRIES = 3; // QX_USB_DESYNC_RESET_TRIES

    // Restart reason, at most 64 bytes (recordRestartReason)
    static constexpr const char* RESTART_REASON = "Q* replies out of step with commands after 3 USB resets";

    QxDesyncPolicy() : _resets(0) {}

    // Result of a Q* command (IQxHostPort::transportResult())
    Action onResult(int ret) {
        if (ret >= 0) {
            _resets = 0;
            return Action::NONE;
        }
        if (ret != QX_USB_ERR_DESYNC) return Action::NONE;
        if (++_resets > RESET_TRIES) return Action::RESTART;
        return Action::RESET_DEVICE;
    }

    // Resets of the current streak, for the log line "(n/3)"
    uint8_t resets() const { return _resets; }

private:
    uint8_t _resets;
};

#endif // QX_DESYNC_POLICY_H
