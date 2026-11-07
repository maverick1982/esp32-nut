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

// megatec_testing[] without the I answer: the transport echoes "I\r" (TESTING branch)
static const QxTesting MEGATEC_NO_VENDOR[] = {
    { "Q1\r", "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", -1 },
    { "F\r", "#230.0 000 024.0 50.0\r", -1 },
    { nullptr, nullptr, 0 }
};

#endif // QX_TESTING_TABLES_H
