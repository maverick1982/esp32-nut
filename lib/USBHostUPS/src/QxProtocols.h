#ifndef QX_PROTOCOLS_H
#define QX_PROTOCOLS_H

#include "QxTypes.h"

/**
 * The ported Q* protocol subdrivers (ADR 0013, section 4), one file each, each a row by row
 * copy of its nut_repo/drivers/nutdrv_qx_<name>.c. The next protocols (mustek, megatec-old,
 * zinto, innovatae with US-134, hunnox with US-138) are added here the same way; the probing
 * order is the one of subdriver_list[] (nutdrv_qx.c:95-117), set by US-134.
 */

extern const QxSubdriver QX_MEGATEC; // nutdrv_qx_megatec.c, "Megatec 0.10"
extern const QxSubdriver QX_Q1;      // nutdrv_qx_q1.c, "Q1 0.08" (fallback)

#endif // QX_PROTOCOLS_H
