#include "QxUsbTransports.h"
#include <string.h>
#include "QxUsbKrauler.h"

namespace {

using Factory = std::unique_ptr<QxUsbTransport> (*)(IQxUsbPrimitives& usb);

struct Row {
    const char* name;  // NUT USB subdriver name
    Factory create;
    uint32_t budgetMs; // the budget of the class, also in the table of QxUsbTransport.h
};

template <typename T>
std::unique_ptr<QxUsbTransport> make(IQxUsbPrimitives& usb) {
    return std::unique_ptr<QxUsbTransport>(new T(usb));
}

// The names of usbsubdriver[] (nut_repo/drivers/nutdrv_qx.c:3376-3392), as the DriverRegistry
// Q* rows use them. The other subdrivers are added with their ports.
const Row ROWS[] = {
    { "krauler", &make<QxKraulerTransport>, QxKraulerTransport::BUDGET_MS }, // US-137
    { "fabula",  &make<QxFabulaTransport>,  QxFabulaTransport::BUDGET_MS },  // US-137
    { "hunnox",  &make<QxHunnoxTransport>,  QxHunnoxTransport::BUDGET_MS },  // US-137
    { nullptr, nullptr, 0 }
};

const size_t ROW_COUNT = sizeof(ROWS) / sizeof(ROWS[0]) - 1;

} // namespace

namespace QxUsbTransports {

std::unique_ptr<QxUsbTransport> create(const char* name, IQxUsbPrimitives& usb) {
    if (!name) return nullptr;
    for (size_t i = 0; i < ROW_COUNT; i++) {
        if (strcmp(ROWS[i].name, name) == 0) return ROWS[i].create(usb);
    }
    return nullptr;
}

size_t count() { return ROW_COUNT; }

const char* nameAt(size_t i) { return i < ROW_COUNT ? ROWS[i].name : nullptr; }

uint32_t budgetAt(size_t i) { return i < ROW_COUNT ? ROWS[i].budgetMs : 0; }

} // namespace QxUsbTransports
