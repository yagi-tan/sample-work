#ifndef OWN_SPI_H
#define OWN_SPI_H

#include "own_spi.pio.h"

#include <hardware/address_mapped.h>

#include <stdint.h>

void own_spi_dma_irq0_handler(io_rw_32 *ints);
void own_spi_pio0_irq0_handler();

bool own_spi_init(PIO pio, uint pin_base, uint32_t bit_rate);
int own_spi_read_blocking(uint8_t reg_addr, uint8_t *data, size_t data_sz);
int own_spi_write_blocking(uint8_t reg_addr, const uint8_t *data, size_t data_sz);

#endif
