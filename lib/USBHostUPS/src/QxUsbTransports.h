#ifndef QX_USB_TRANSPORTS_H
#define QX_USB_TRANSPORTS_H

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include "QxUsbTransport.h"

/**
 * @brief The Q* USB subdrivers ported so far, by their NUT name (the qx_transport of the
 * DriverRegistry Q* rows). A name not listed has no transport yet: the host keeps the UPS on
 * the "not supported yet" path of US-132.
 */
namespace QxUsbTransports {
    // New transport on usb, nullptr when the name is not ported
    std::unique_ptr<QxUsbTransport> create(const char* name, IQxUsbPrimitives& usb);
    // The table, for the tests: count, names and budgets (QxUsbTransport::commandBudgetMs())
    size_t count();
    const char* nameAt(size_t i);
    uint32_t budgetAt(size_t i);
}

#endif // QX_USB_TRANSPORTS_H
