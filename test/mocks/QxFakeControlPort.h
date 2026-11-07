#ifndef QX_FAKE_CONTROL_PORT_H
#define QX_FAKE_CONTROL_PORT_H

#include <string>
#include <vector>
#include "IUpsControlPort.h"
#include "QxControl.h"
#include "QxEngine.h"

/**
 * @brief The host as QxControl sees it (US-135), for the native tests: the commands and
 * SET VAR of the driver run at once on the engine (the real host runs them in the poll task,
 * US-136). executeCommand() is the host's INSTCMD: it records the call and goes back into
 * QxControl, as USBHostUPS does through activeControl(). The HID accessor and the beeper
 * write count their calls: a Q* control must never use them.
 */
class QxFakeControlPort : public IUpsControlPort {
public:
    QxFakeControlPort(QxEngine& engine, UPSData& data) : _engine(engine), _data(data) {}

    bool connected = true;
    bool recognised = true;           // a protocol was recognised (driverCommands not empty)
    std::vector<std::string> commands; // INSTCMDs seen by the host, "name" or "name param"
    std::vector<std::string> defaults; // noteDefaultShutdown() calls
    mutable int hidCalls = 0;
    int beeperCalls = 0;

    void lock() const override {}
    void unlock() const override {}
    bool isConnected() const override { return connected; }
    const UPSData& upsData() const override { return _data; }

    bool setBeeper(bool enable) override {
        (void)enable;
        beeperCalls++;
        return false;
    }

    CommandResult executeCommand(const char* name, const char* param = nullptr) override {
        std::string c = name ? name : "(null)";
        if (param && *param) c += std::string(" ") + param;
        commands.push_back(c);
        return QxControl::instance().executeCommand(*this, name, param);
    }

    void noteDefaultShutdown(const char* via) override { defaults.push_back(via ? via : "(null)"); }

    std::vector<const UPSCommandInfo*> driverCommands() const override {
        if (!recognised) return {};
        return QxControl::commandsOf(_engine);
    }
    CommandResult runDriverCommand(const char* name, const char* param) override {
        return QxControl::runCommand(_engine, _data, name, param);
    }
    NutTable::SetStatus runDriverSetVariable(const char* name, const char* value) override {
        return QxControl::runSetVariable(_engine, _data, name, value);
    }

    IHidControlPort* hidPort() override {
        hidCalls++;
        return nullptr;
    }
    const IHidControlPort* hidPort() const override {
        hidCalls++;
        return nullptr;
    }

private:
    QxEngine& _engine;
    UPSData& _data;
};

#endif // QX_FAKE_CONTROL_PORT_H
