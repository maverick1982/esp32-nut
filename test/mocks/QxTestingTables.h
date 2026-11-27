/*
 * esp32-nut
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Contains code and data derived from Network UPS Tools (https://networkupstools.org/),
 * GPL-2.0-or-later, modified for esp32-nut in 2026.
 *
 * drivers/nutdrv_qx_megatec.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *
 * drivers/nutdrv_qx_megatec-old.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *
 * drivers/nutdrv_qx_mustek.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *
 * drivers/nutdrv_qx_zinto.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *
 * drivers/nutdrv_qx_hunnox.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 *                 2020 Mariano Jan https://marianojan.com
 *
 * drivers/nutdrv_qx_innovatae.c:
 *   Copyright (C) 2025 Viktor Drobot <linux776@gmail.com>
 *
 * drivers/nutdrv_qx_voltronic.c:
 *   Copyright (C) 2013 Daniele Pezzini <hyouko@gmail.com>
 */

#ifndef QX_TESTING_TABLES_H
#define QX_TESTING_TABLES_H

#include "QxTestingTransport.h"

// The testing_t tables of the ported Q* protocols (#ifdef TESTING in each
// nut_repo/drivers/nutdrv_qx_<name>.c), copied as they are. Shared by test_qx_protocols
// (replay of each protocol) and test_qx_probe (a fake UPS of each kind for the probing).

// megatec_testing[], nut_repo/drivers/nutdrv_qx_megatec.c:118-133
static const QxTesting MEGATEC_TESTING[] = {
    { "Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "F\r", "#230.0 000 024.0 50.0\r", -1 },
    { "I\r", "#NOT_A_LIVE_UPS  TESTING    TESTING   \r", -1 },
    { "Q\r", "", -1 },
    { "S03\r", "", -1 },
    { "C\r", "", -1 },
    { "S02R0005\r", "", -1 },
    { "S.5R0000\r", "", -1 },
    { "T04\r", "", -1 },
    { "TL\r", "", -1 },
    { "T\r", "", -1 },
    { "CT\r", "", -1 },
    { nullptr, nullptr, 0 }
};

// q1_testing[], nut_repo/drivers/nutdrv_qx_q1.c:93-106
static const QxTesting Q1_TESTING[] = {
    { "Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "Q\r", "", -1 },
    { "S03\r", "", -1 },
    { "C\r", "", -1 },
    { "S02R0005\r", "", -1 },
    { "S.5R0000\r", "", -1 },
    { "T04\r", "", -1 },
    { "TL\r", "", -1 },
    { "T\r", "", -1 },
    { "CT\r", "", -1 },
    { nullptr, nullptr, 0 }
};

// mustek_testing[], nut_repo/drivers/nutdrv_qx_mustek.c:102-116
static const QxTesting MUSTEK_TESTING[] = {
    { "QS\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "F\r", "#230.0 000 024.0 50.0\r", -1 },
    { "I\r", "#NOT_A_LIVE_UPS  TESTING    TESTING   \r", -1 },
    { "Q\r", "", -1 },
    { "S03\r", "", -1 },
    { "C\r", "", -1 },
    { "S02R0005\r", "", -1 },
    { "S.5R0000\r", "", -1 },
    { "T04\r", "", -1 },
    { "TL\r", "", -1 },
    { "T\r", "", -1 },
    { "CT\r", "", -1 },
    { nullptr, nullptr, 0 }
};

// megatec_old_testing[], nut_repo/drivers/nutdrv_qx_megatec-old.c:102-116
static const QxTesting MEGATEC_OLD_TESTING[] = {
    { "D\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "F\r", "#230.0 000 024.0 50.0\r", -1 },
    { "I\r", "#NOT_A_LIVE_UPS  TESTING    TESTING   \r", -1 },
    { "Q\r", "", -1 },
    { "S03\r", "", -1 },
    { "C\r", "", -1 },
    { "S02R0005\r", "", -1 },
    { "S.5R0000\r", "", -1 },
    { "T04\r", "", -1 },
    { "TL\r", "", -1 },
    { "T\r", "", -1 },
    { "CT\r", "", -1 },
    { nullptr, nullptr, 0 }
};

// zinto_testing[], nut_repo/drivers/nutdrv_qx_zinto.c:102-116
static const QxTesting ZINTO_TESTING[] = {
    { "Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "F\r", "#230.0 000 024.0 50.0\r", -1 },
    { "FW?\r", "#NOT_A_LIVE_UPS  TESTING    TESTING   \r", -1 },
    { "Q\r", "", -1 },
    { "S03\r", "", -1 },
    { "C\r", "", -1 },
    { "S02R0005\r", "", -1 },
    { "S.5R0000\r", "", -1 },
    { "T04\r", "", -1 },
    { "TL\r", "", -1 },
    { "T\r", "", -1 },
    { "CT\r", "", -1 },
    { nullptr, nullptr, 0 }
};

// hunnox_testing[], nut_repo/drivers/nutdrv_qx_hunnox.c:130-146
static const QxTesting HUNNOX_TESTING[] = {
    { "Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "F\r", "#230.0 000 024.0 50.0\r", -1 },
    { "FW?\r", "#NOT_A_LIVE_UPS  TESTING    TESTING   \r", -1 },
    { "Q\r", "", -1 },
    { "S03\r", "", -1 },
    { "C\r", "", -1 },
    { "S02R0005\r", "", -1 },
    { "S.5R0000\r", "", -1 },
    { "T04\r", "", -1 },
    { "TL\r", "", -1 },
    { "T\r", "", -1 },
    { "CT\r", "", -1 },
    { "BL\r", "BL100\r", -1 },
    { nullptr, nullptr, 0 }
};

// innovatae_testing[], nut_repo/drivers/nutdrv_qx_innovatae.c:112-126
static const QxTesting INNOVATAE_TESTING[] = {
    { "Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "Q\r", "", -1 },
    { "F\r", "#230.0 004 024.0 50.0\r", -1 },
    { "I\r", "#222222222222222222222222222R1.00.48\r", -1 },
    { "S03\r", "", -1 },
    { "C\r", "", -1 },
    { "S02R0005\r", "", -1 },
    { "S.5R0000\r", "", -1 },
    { "T04\r", "ACK", -1 },
    { "TL\r", "ACK", -1 },
    { "T\r", "ACK", -1 },
    { "CT\r", "ACK", -1 },
    { nullptr, nullptr, 0 }
};

// voltronic_testing[], nut_repo/drivers/nutdrv_qx_voltronic.c:1562-1679
static const QxTesting VOLTRONIC_TESTING[] = {
    { "QGS\r", "(234.9 50.0 229.8 50.0 000.0 00A 369.1 ---.- 026.5 ---.- 018.8 100000000001\r", -1 },
    { "QPI\r", "(PI01\r", -1 },
    { "QRI\r", "(230.0 004 024.0 50.0\r", -1 },
    { "QMF\r", "(#####VOLTRONIC\r", -1 },
    { "I\r", "#-------------   ------     VT12046Q  \r", -1 },
    { "F\r", "#220.0 000 024.0 50.0\r", -1 },
    { "QMD\r", "(#######OLHVT1K0 ###1000 80 2/2 230 230 02 12.0\r", -1 },
    { "QFS\r", "(14 212.1 50.0 005.6 49.9 006 010.6 343.8 ---.- 026.2 021.8 01101100\r", -1 },
    { "QMOD\r", "(S\r", -1 },
    { "QVFW\r", "(VERFW:00322.02\r", -1 },
    { "QID\r", "(685653211455\r", -1 },
    { "QBV\r", "(026.5 02 01 068 255\r", -1 },
    { "QFLAG\r", "(EpashcjDbroegfl\r", -1 },
    { "QWS\r", "(0000000000000000000000000000000000000000000000000000000001000001\r", -1 },
    { "QHE\r", "(242 218\r", -1 },
    { "QBYV\r", "(264 170\r", -1 },
    { "QBYF\r", "(53.0 47.0\r", -1 },
    { "QSK1\r", "(1\r", -1 },
    { "QSK2\r", "(0\r", -1 },
    { "QSK3\r", "(1\r", -1 },
    { "QSK4\r", "(NAK\r", -1 },
    { "QSKT1\r", "(008\r", -1 },
    { "QSKT2\r", "(012\r", -1 },
    { "QSKT3\r", "(NAK\r", -1 },
    { "QSKT4\r", "(007\r", -1 },
    { "RE0\r", "#20\r", -1 },
    { "W0E24\r", "(ACK\r", -1 },
    { "PF\r", "(ACK\r", -1 },
    { "PEA\r", "(ACK\r", -1 },
    { "PDR\r", "(NAK\r", -1 },
    { "HEH250\r", "(ACK\r", -1 },
    { "HEL210\r", "(ACK\r", -1 },
    { "PHV260\r", "(NAK\r", -1 },
    { "PLV190\r", "(ACK\r", -1 },
    { "PGF51.0\r", "(NAK\r", -1 },
    { "PSF47.5\r", "(ACK\r", -1 },
    { "BATN2\r", "(ACK\r", -1 },
    { "BATGN04\r", "(ACK\r", -1 },
    { "QBT\r", "(01\r", -1 },
    { "PBT02\r", "(ACK\r", -1 },
    { "QGR\r", "(00\r", -1 },
    { "PGR01\r", "(ACK\r", -1 },
    { "PSK1008\r", "(ACK\r", -1 },
    { "PSK3987\r", "(ACK\r", -1 },
    { "PSK2009\r", "(ACK\r", -1 },
    { "PSK4012\r", "(ACK\r", -1 },
    { "Q3PV\r", "(123.4 456.4 789.4 012.4 323.4 223.4\r", -1 },
    { "Q3OV\r", "(253.4 163.4 023.4 143.4 103.4 523.4\r", -1 },
    { "Q3OC\r", "(109 069 023\r", -1 },
    { "Q3LD\r", "(005 033 089\r", -1 },
    { "Q3YV\r", "(303.4 245.4 126.4 222.4 293.4 321.4\r", -1 },
    { "Q3PC\r", "(002 023 051\r", -1 },
    { "SOFF\r", "(NAK\r", -1 },
    { "SON\r", "(ACK\r", -1 },
    { "T\r", "(NAK\r", -1 },
    { "TL\r", "(ACK\r", -1 },
    { "CS\r", "(ACK\r", -1 },
    { "CT\r", "(NAK\r", -1 },
    { "BZOFF\r", "(ACK\r", -1 },
    { "BZON\r", "(ACK\r", -1 },
    { "S.3R0002\r", "(ACK\r", -1 },
    { "S02R0024\r", "(NAK\r", -1 },
    { "S.5\r", "(ACK\r", -1 },
    { "T.3\r", "(ACK\r", -1 },
    { "T02\r", "(NAK\r", -1 },
    { "SKON1\r", "(ACK\r", -1 },
    { "SKOFF1\r", "(NAK\r", -1 },
    { "SKON2\r", "(ACK\r", -1 },
    { "SKOFF2\r", "(ACK\r", -1 },
    { "SKON3\r", "(NAK\r", -1 },
    { "SKOFF3\r", "(ACK\r", -1 },
    { "SKON4\r", "(NAK\r", -1 },
    { "SKOFF4\r", "(NAK\r", -1 },
    { "QPAR\r", "(003\r", -1 },
    { "QPD\r", "(000 240\r", -1 },
    { "PPD120\r", "(ACK\r", -1 },
    { "QLDL\r", "(005 080\r", -1 },
    { "QBDR\r", "(1234\r", -1 },
    { "QFRE\r", "(50.0 00.0\r", -1 },
    { "FREH54.0\r", "(ACK\r", -1 },
    { "FREL47.0\r", "(ACK\r", -1 },
    { "PEP\r", "(ACK\r", -1 },
    { "PDP\r", "(ACK\r", -1 },
    { "PEB\r", "(ACK\r", -1 },
    { "PDB\r", "(ACK\r", -1 },
    { "PER\r", "(NAK\r", -1 },
    { "PDR\r", "(NAK\r", -1 },
    { "PEO\r", "(ACK\r", -1 },
    { "PDO\r", "(ACK\r", -1 },
    { "PEA\r", "(ACK\r", -1 },
    { "PDA\r", "(ACK\r", -1 },
    { "PES\r", "(ACK\r", -1 },
    { "PDS\r", "(ACK\r", -1 },
    { "PEV\r", "(ACK\r", -1 },
    { "PDV\r", "(ACK\r", -1 },
    { "PEE\r", "(ACK\r", -1 },
    { "PDE\r", "(ACK\r", -1 },
    { "PEG\r", "(ACK\r", -1 },
    { "PDG\r", "(NAK\r", -1 },
    { "PED\r", "(ACK\r", -1 },
    { "PDD\r", "(ACK\r", -1 },
    { "PEC\r", "(ACK\r", -1 },
    { "PDC\r", "(NAK\r", -1 },
    { "PEF\r", "(NAK\r", -1 },
    { "PDF\r", "(ACK\r", -1 },
    { "PEJ\r", "(NAK\r", -1 },
    { "PDJ\r", "(ACK\r", -1 },
    { "PEL\r", "(ACK\r", -1 },
    { "PDL\r", "(ACK\r", -1 },
    { "PEN\r", "(ACK\r", -1 },
    { "PDN\r", "(ACK\r", -1 },
    { "PEQ\r", "(ACK\r", -1 },
    { "PDQ\r", "(ACK\r", -1 },
    { "PEW\r", "(NAK\r", -1 },
    { "PDW\r", "(ACK\r", -1 },
    { nullptr, nullptr, 0 }
};

// megatec_testing[] without the I answer: the transport echoes "I\r" (TESTING branch)
static const QxTesting MEGATEC_NO_VENDOR[] = {
    { "Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "F\r", "#230.0 000 024.0 50.0\r", -1 },
    { nullptr, nullptr, 0 }
};

#endif // QX_TESTING_TABLES_H
