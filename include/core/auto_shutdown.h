#ifndef CORE_AUTO_SHUTDOWN_H
#define CORE_AUTO_SHUTDOWN_H

#include <string.h>

/**
 * @brief When the board shuts the UPS down by itself on low battery (US-114).
 *
 * Pure logic, so it runs in the native unit tests. With the option on, the board sends
 * shutdown.default once when ups.status holds both OB and LB, and not again until the
 * status is back to OL: that is one event. The threshold is the one of the UPS
 * (battery.charge.low / battery.runtime.low sets its LB bit), there is no other.
 *
 * - armed starts true and only an OL status re-arms it: turning the option off and on
 *   during an event already handled does not send the command again;
 * - a disconnected UPS or stale data change nothing, in either direction;
 * - restoreArm() is for a command that did not reach the UPS (NOT_CONNECTED). Every other
 *   outcome uses up the event.
 */
class AutoShutdown {
public:
    AutoShutdown() : _enabled(false), _armed(true) {}

    void setEnabled(bool enabled) { _enabled = enabled; }
    bool enabled() const { return _enabled; }
    bool armed() const { return _armed; }

    /**
     * @param connected the UPS is connected
     * @param stale the data of the UPS are stale
     * @param status ups.status, words separated by spaces (nullptr: no word)
     * @param command_available the UPS offers shutdown.default
     * @return true when shutdown.default must be sent now
     */
    bool update(bool connected, bool stale, const char* status, bool command_available) {
        if (!connected || stale) return false;
        if (hasToken(status, "OL")) {
            _armed = true;
            return false;
        }
        if (onBatteryLow(status) && _armed && _enabled && command_available) {
            _armed = false;
            return true;
        }
        return false;
    }

    /** @brief The command did not reach the UPS: the event is not used up. */
    void restoreArm() { _armed = true; }

    /** @brief status holds token as a whole word (never a part of another word). */
    static bool hasToken(const char* status, const char* token) {
        if (!status || !token || !*token) return false;
        const size_t n = strlen(token);
        const char* p = status;
        while (*p) {
            while (*p == ' ') p++;
            const char* start = p;
            while (*p && *p != ' ') p++;
            if ((size_t)(p - start) == n && strncmp(start, token, n) == 0) return true;
        }
        return false;
    }

    /** @brief On battery with low battery: OB and LB. */
    static bool onBatteryLow(const char* status) {
        return hasToken(status, "OB") && hasToken(status, "LB");
    }

    /**
     * @brief The command catalog holds shutdown.default. Commands is what
     * getSupportedCommands() returns (pointers to items with a name): a template, so this
     * header needs neither UpsCommands.h nor Arduino. nullptr items and names are skipped.
     */
    template <class Commands>
    static bool offersShutdownDefault(const Commands& commands) {
        for (const auto* c : commands) {
            if (c && c->name && strcmp(c->name, "shutdown.default") == 0) return true;
        }
        return false;
    }

private:
    bool _enabled;
    bool _armed;
};

/**
 * @brief Whether the option can be turned on (US-114). The UPS offers a shutdown command
 * when it offers shutdown.default: HidControl::buildCommands() publishes it exactly when
 * one of the commands it tries is there. Without a UPS the commands are not known.
 */
enum class AutoShutdownAvailability { AVAILABLE, NO_UPS, NO_COMMAND };

inline AutoShutdownAvailability autoShutdownAvailability(bool connected, bool has_shutdown_default) {
    if (!connected) return AutoShutdownAvailability::NO_UPS;
    return has_shutdown_default ? AutoShutdownAvailability::AVAILABLE : AutoShutdownAvailability::NO_COMMAND;
}

#endif // CORE_AUTO_SHUTDOWN_H
