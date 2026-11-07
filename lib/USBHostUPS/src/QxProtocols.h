#ifndef QX_PROTOCOLS_H
#define QX_PROTOCOLS_H

#include "QxTypes.h"

/**
 * The ported Q* protocol subdrivers (ADR 0013, section 4), one file each, each a row by row
 * copy of its nut_repo/drivers/nutdrv_qx_<name>.c. The next protocols (on user request) are
 * added here the same way and get their place in QX_SUBDRIVER_LIST, the probing order of
 * subdriver_list[] (nutdrv_qx.c:95-117).
 */

extern const QxSubdriver QX_MUSTEK;      // nutdrv_qx_mustek.c, "Mustek 0.08"
extern const QxSubdriver QX_MEGATEC_OLD; // nutdrv_qx_megatec-old.c, "Megatec/old 0.08"
extern const QxSubdriver QX_MEGATEC;     // nutdrv_qx_megatec.c, "Megatec 0.10"
extern const QxSubdriver QX_ZINTO;       // nutdrv_qx_zinto.c, "Zinto 0.07"
extern const QxSubdriver QX_HUNNOX;      // nutdrv_qx_hunnox.c, "Hunnox 0.03" (US-138)
extern const QxSubdriver QX_INNOVATAE;   // nutdrv_qx_innovatae.c, "INNOVATAE 0.01"
extern const QxSubdriver QX_Q1;          // nutdrv_qx_q1.c, "Q1 0.08" (fallback)

/**
 * The ported protocols in the order of subdriver_list[] (nutdrv_qx.c:95-117), ended by
 * nullptr. The protocols not ported yet are comments in their upstream position: the
 * probing skips them but never reorders the others (ADR 0013).
 */
extern const QxSubdriver* const QX_SUBDRIVER_LIST[];

/**
 * @brief The protocol filter of subdriver_matcher() (nutdrv_qx.c:4329-4345): the protocol
 * name up to the first space, compared case-insensitively with protocol.
 *
 * The names are the ones of protocol in ups.conf: "mustek", "megatec/old" (with the slash,
 * not "megatec-old"), "megatec", "zinto", "innovatae", "q1". A null protocol never matches.
 */
bool qxProtocolMatches(const QxSubdriver& sub, const char* protocol);

// Room for a protocol name of the list and its NUL ("megatec/old" is the longest, 11 chars)
static const size_t QX_PROTOCOL_NAME_MAX = 16;

/**
 * @brief The ups.conf name of a protocol (US-138): sub.name up to the first space, lower
 * case ("Megatec/old 0.08" -> "megatec/old", "INNOVATAE 0.01" -> "innovatae").
 * @return The length written (truncated to outlen - 1); 0 with outlen 0
 */
size_t qxProtocolName(const QxSubdriver& sub, char* out, size_t outlen);

/**
 * @brief The protocol of list whose name is name, as qxProtocolMatches() (case-insensitive).
 * @return nullptr for nullptr, "" or a name not in the list
 */
const QxSubdriver* qxFindProtocol(const char* name, const QxSubdriver* const* list = QX_SUBDRIVER_LIST);

#endif // QX_PROTOCOLS_H
