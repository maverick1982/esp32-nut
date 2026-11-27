#ifndef QX_TRANSPORT_H
#define QX_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>
#include "IQxTransport.h"
#include "QxTypes.h"

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
 * @brief The transport a QxDriver runs on (ADR 0014, section 6): one Q* command and its
 * answer, with what the driver reports about the transport.
 *
 * The family of the device is behind it: QxUsbTransport for the USB subdrivers of
 * nutdrv_qx (HID bridges, ADR 0013), QxSerialTransport for a USB serial bridge (the serial
 * path of qx_command(), ADR 0014). QxDriver names neither.
 */
class QxTransport : public IQxTransport {
public:
    // Name of the transport: the NUT USB subdriver ("krauler"), or "serial"
    virtual const char* name() const = 0;
    // Budget of one command; the poll step bound is qxStepBoundMs(budget)
    virtual uint32_t commandBudgetMs() const = 0;
    // True when the answers come on the interrupt IN endpoint: the host starts the IN pipe
    virtual bool needsInterruptIn() const { return false; }
    // The ups.conf options the transport reads with testvar() (US-138): none by default,
    // novendor for hunnox. Called by QxDriver::configure() in the poll task, between commands
    virtual void setOptions(const QxOptions& options) { (void)options; }
    // The langid_fix in use (issue #91): 0 as NUT without the option, 0x0409 once the
    // krauler or hunnox transport has switched to it by itself (QxLangidFallback)
    virtual uint16_t langidFix() const { return 0; }
    // VID/PID of the attached device, from QxDriver's constructor before any command: the
    // defaults upsdrv_initups() picks by USB ID (the cypress 0665:5161 quirk, US-141)
    virtual void setDevice(uint16_t vid, uint16_t pid) {
        (void)vid;
        (void)pid;
    }
    // A line for the System Log left by the last command (upslogx() of the subdriver), once;
    // nullptr when there is none. QxDriver::Recorder logs it as INFO
    virtual const char* takeNotice() { return nullptr; }
    // The last string read refused by usb_get_string_simple() (issue #91); not present by default
    virtual const QxStringFailure& stringFailure() const {
        static const QxStringFailure none;
        return none;
    }
    // Text of the last error for the diagnostics export ("ESP_ERR_TIMEOUT", "budget", ...)
    virtual const char* lastError() const = 0;
    virtual uint32_t nowMs() const = 0;
};

#endif // QX_TRANSPORT_H
