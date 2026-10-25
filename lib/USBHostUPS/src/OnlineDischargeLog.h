#ifndef ONLINE_DISCHARGE_LOG_H
#define ONLINE_DISCHARGE_LOG_H

/**
 * @brief When to log that the UPS is in OL+DISCHRG (issue #98).
 *
 * Pure logic, so it runs in the native unit tests. Mirrors the warning of
 * usbhid-ups.c ups_status_set() with its default throttle
 * (onlinedischarge_log_throttle_sec -2, onlinedischarge_log_throttle_hovercharge 100):
 *
 * - a message only when battery.charge is served, above 0, under HOVER_CHARGE and
 *   different from the charge of the previous message. A fully charged UPS that
 *   reports Discharging on mains (APC BGM2200) stays quiet;
 * - without battery.charge upstream never logs: its 30 s time throttle only starts
 *   after a first message, which needs the charge;
 * - the state resets when Discharging drops, as onlinedischarge_log_throttle_charge.
 */
class OnlineDischargeLog {
public:
    static const int HOVER_CHARGE = 100;

    OnlineDischargeLog() : _lastCharge(-1), _prevCharge(-1) {}

    /**
     * @param charge battery.charge rounded to a whole percent, -1 when not served
     * @return true when the warning is due now
     */
    bool update(bool acPresent, bool discharging, int charge) {
        if (!discharging) {
            _lastCharge = -1;
            return false;
        }
        if (!acPresent) return false; // OB DISCHRG, nothing unusual
        if (charge > 0 && charge != _lastCharge && charge < HOVER_CHARGE) {
            _prevCharge = _lastCharge;
            _lastCharge = charge;
            return true;
        }
        return false;
    }

    /** @brief Charge of the last message. */
    int charge() const { return _lastCharge; }
    /** @brief Charge of the message before, -1 for the first one of a sequence. */
    int previousCharge() const { return _prevCharge; }

private:
    int _lastCharge;
    int _prevCharge;
};

#endif // ONLINE_DISCHARGE_LOG_H
