#include "QxProtocols.h"
#include <stdio.h>
#include <string.h>
#include "QxEngine.h"

// subdriver_list[] of nut_repo/drivers/nutdrv_qx.c:95-117, same order. The protocols not
// ported yet stay as comments in their upstream position, so a port only uncomments a line.
// Their claims come first upstream for some units (the vendor-gated voltronic family, for
// example): until they are ported such a unit can be claimed by a later protocol here.
const QxSubdriver* const QX_SUBDRIVER_LIST[] = {
    /* Vendor-ID-gated before command-based protocol probes */
    // &omron_subdriver,
    // &voltronic_subdriver,
    // &voltronic_axpert_subdriver,
    // &voltronic_qs_subdriver,
    // &voltronic_qs_hex_subdriver,
    &QX_MUSTEK,
    &QX_MEGATEC_OLD,
    // &bestups_subdriver,
    // &mecer_subdriver,
    &QX_MEGATEC,
    &QX_ZINTO,
    // &masterguard_subdriver,
    &QX_HUNNOX,
    // &ablerex_subdriver,
    // &innovart31_subdriver,
    // &innovart33_subdriver,
    &QX_INNOVATAE,
    // &q2_subdriver,
    // &q6_subdriver,
    // &gtec_subdriver,
    /* Fallback Q1 subdriver */
    &QX_Q1,
    nullptr
};

// nutdrv_qx.c:4332-4343: "Get rid of subdriver version", then strcasecmp()
bool qxProtocolMatches(const QxSubdriver& sub, const char* protocol) {
    if (!protocol || !sub.name) return false;
    // SMALLBUF upstream; the names are a few characters, a smaller buffer spares the stack
    // of the poll task (a longer name would only be truncated and not match)
    char name[64];
    snprintf(name, sizeof(name), "%.*s", (int)strcspn(sub.name, " "), sub.name);
    return QxEngine::equalsIgnoreCase(name, protocol);
}

size_t qxProtocolName(const QxSubdriver& sub, char* out, size_t outlen) {
    if (!out || outlen == 0) return 0;
    size_t n = 0;
    if (sub.name) {
        size_t len = strcspn(sub.name, " ");
        for (; n < len && n + 1 < outlen; n++) {
            char c = sub.name[n];
            out[n] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
    }
    out[n] = '\0';
    return n;
}

const QxSubdriver* qxFindProtocol(const char* name, const QxSubdriver* const* list) {
    if (!name || !*name || !list) return nullptr;
    for (const QxSubdriver* const* s = list; *s; s++) {
        if (qxProtocolMatches(**s, name)) return *s;
    }
    return nullptr;
}
