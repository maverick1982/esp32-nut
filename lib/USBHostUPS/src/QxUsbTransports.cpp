#include "QxUsbTransports.h"
#include <string.h>

namespace {

using Factory = std::unique_ptr<QxUsbTransport> (*)(IQxUsbPrimitives& usb);

struct Row {
    const char* name;  // NUT USB subdriver name
    Factory create;
    uint32_t budgetMs; // the budget of the class, also in the table of QxUsbTransport.h
};

// US-137 adds krauler, fabula and hunnox here. The sentinel keeps the table valid while it
// has no row.
const Row ROWS[] = {
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
