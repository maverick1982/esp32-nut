#ifndef QX_USB_TRANSPORT_H
#define QX_USB_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include "IQxTransport.h"
#include "IQxUsbPrimitives.h"
#include "QxTypes.h"

// Timeout of one control transfer of hid_host (USBUPS_CTRL_TIMEOUT_MS, hid_host.c:81-84)
static const uint32_t QX_CTRL_TIMEOUT_MS = 1500;
// Longest wait of one poll step on the Q* path, well inside the 30 s Task WDT (ADR 0008)
static const uint32_t QX_STEP_BOUND_MAX_MS = 12000;

/**
 * @brief Longest wait of one poll step for a transport with the given command budget.
 *
 * A step sends at most two Q* commands (a claim, or the fallbacks of instcmd()), the second
 * only after an answer to the first; a transfer started just before the deadline still ends
 * within its own timeout.
 */
inline uint32_t qxStepBoundMs(uint32_t budgetMs) { return 2 * budgetMs + QX_CTRL_TIMEOUT_MS; }

/**
 * @brief The last string read that usb_get_string_simple() refused (issue #91), for the
 * diagnostics export: its first bytes tell a broken langid list from a malformed answer.
 */
struct QxStringFailure {
    static const size_t BYTES_MAX = 16;
    bool present = false;
    uint8_t index = 0;          // string index of the command
    int result = 0;             // what qxGetStringAscii() returned
    int langidsLength = 0;      // string descriptor 0: bytes received, or an error < 0
    uint8_t langids[BYTES_MAX] = {0};
    bool answerAsked = false;   // false: the read stopped at string descriptor 0
    int answerLength = 0;       // the string: bytes received, or an error < 0
    uint8_t answer[BYTES_MAX] = {0};
};

/**
 * @brief A Q* USB subdriver (ADR 0013, section 3): one class per upstream *_command() of
 * nut_repo/drivers/nutdrv_qx.c, built only on IQxUsbPrimitives.
 *
 * command() gives each command its budget: the primitives refuse new transfers past it, so a
 * transport that retries (krauler up to 10 times on corrupt answers) stops there. This only
 * differs from NUT when the device does not answer, or answers badly, until the deadline: the
 * command would fail anyway.
 *
 * Budgets per transport (US-136 plan; the poll step bound is qxStepBoundMs(budget)):
 *   test loopback (test/mocks)              2000 ms -> 5.5 s
 *   krauler, fabula (US-137)                3000 ms -> 7.5 s   2 GET_DESCRIPTOR per command
 *                                                              (langid + index), each <= 1.5 s
 *   hunnox (US-137)                         4500 ms -> 10.5 s  preamble up to 3 GET_DESCRIPTOR + 2
 *   SET_REPORT + interrupt IN (cypress...) <= 4500 ms, set when ported
 */
class QxUsbTransport : public IQxTransport {
public:
    explicit QxUsbTransport(IQxUsbPrimitives& usb) : _usb(usb) {}

    int command(const char* cmd, size_t cmdlen, char* buf, size_t buflen) final {
        _usb.setDeadline(_usb.nowMs() + commandBudgetMs());
        int ret = doCommand(cmd, cmdlen, buf, buflen);
        _usb.setDeadline(0);
        return ret;
    }

    // Name of the NUT USB subdriver ("krauler")
    virtual const char* name() const = 0;
    // Budget of one command, from the table above
    virtual uint32_t commandBudgetMs() const = 0;
    // True when the answers come on the interrupt IN endpoint: the host starts the IN pipe
    virtual bool needsInterruptIn() const { return false; }
    // The ups.conf options the USB subdriver reads with testvar() (US-138): none by default,
    // novendor for hunnox. Called by QxDriver::configure() in the poll task, between commands
    virtual void setOptions(const QxOptions& options) { (void)options; }
    // The langid_fix in use (issue #91): 0 as NUT without the option, 0x0409 once the
    // krauler or hunnox transport has switched to it by itself (QxLangidFallback)
    virtual uint16_t langidFix() const { return 0; }
    // The last string read refused by usb_get_string_simple() (issue #91); not present by default
    virtual const QxStringFailure& stringFailure() const {
        static const QxStringFailure none;
        return none;
    }

    const char* lastError() const { return _usb.lastError(); }
    uint32_t nowMs() const { return _usb.nowMs(); }

protected:
    // The ported *_command()
    virtual int doCommand(const char* cmd, size_t cmdlen, char* buf, size_t buflen) = 0;

    IQxUsbPrimitives& _usb;
};

#endif // QX_USB_TRANSPORT_H
