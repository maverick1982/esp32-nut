/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx_blazer-common.h:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 */

#ifndef QX_BLAZER_COMMON_H
#define QX_BLAZER_COMMON_H

#include <stddef.h>
#include "QxTypes.h"

/**
 * Port of nut_repo/drivers/nutdrv_qx_blazer-common.c: the functions shared by the
 * Megatec-derived Q* protocols (ADR 0013, section 4), ported once. The protocol tables point
 * to them; the probing (US-134) uses the claims, instcmd()/setvar() (US-135) the command
 * and setvar preprocess.
 */

// Range for ups.delay.start / ups.delay.shutdown (blazer_r_ondelay, blazer_r_offdelay)
extern const QxInfoRw BLAZER_R_ONDELAY[];
extern const QxInfoRw BLAZER_R_OFFDELAY[];

// blazer_claim(): status (input.voltage) and vendor (ups.firmware) unless novendor
bool blazerClaim(QxEngine& qx);
// blazer_claim_light(): status only
bool blazerClaimLight(QxEngine& qx);

// blazer_initups() / blazer_initups_light(): norating, novendor, ignoresab skip their rows
void blazerInitups(QxEngine& qx);
void blazerInitupsLight(QxEngine& qx);

// Preprocess functions
int blazerProcessSetvar(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
int blazerProcessCommand(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);
int blazerProcessStatusBits(QxEngine& qx, const QxItem& item, char* value, size_t valuelen);

#endif // QX_BLAZER_COMMON_H
