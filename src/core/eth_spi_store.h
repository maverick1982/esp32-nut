#ifndef CORE_ETH_SPI_STORE_H
#define CORE_ETH_SPI_STORE_H

#include "board/eth_spi_settings.h"

/**
 * W5500 settings of the Network page (US-126), kept in the NVS namespace "eth", outside
 * config_json, so they survive a restart and an OTA update: on (bool), sck, mosi, miso,
 * cs, irq, rst (int8, -1 = not set). load() turns off a module whose saved pins the board
 * does not allow (PDR 0002 D4: a wrong value must not lock the user out), keeping the pins
 * so the user can fix them.
 */
class EthSpiStore {
public:
    EthSpiSettings load(const BoardConfig& board);
    bool save(const EthSpiSettings& settings);
};

#endif // CORE_ETH_SPI_STORE_H
