#ifndef I_QX_HOST_PORT_H
#define I_QX_HOST_PORT_H

/**
 * @brief The host as the Q* driver sees it (US-136): the lock of the UPS data it shares with
 * the readers (the recursive _mutex, ADR 0008) and the System Log.
 *
 * The driver takes the lock only to publish its data and its diagnostics, never around a Q*
 * command.
 */
class IQxHostPort {
public:
    virtual ~IQxHostPort() = default;
    virtual void lock() const = 0;
    virtual void unlock() const = 0;
    // level as USBHostUPS::log() ("INFO", "WARN", "ERROR"), text without trailing newline
    virtual void log(const char* level, const char* text) const = 0;
    // True during the link back-off (USBHostUPS::isPollingPaused()): the driver skips its
    // step, as the HID drivers do, instead of counting refused transfers as failed attempts
    virtual bool pollingPaused() const { return false; }
};

#endif // I_QX_HOST_PORT_H
