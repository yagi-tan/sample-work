#ifndef OWN_I2C_H
#define OWN_I2C_H

#include "own_i2c.pio.h"

#include <hardware/address_mapped.h>

#include <stdint.h>

void own_i2c_dma_irq0_handler(io_rw_32 *ints);
void own_i2c_pio0_irq0_handler();

bool own_i2c_init(PIO pio, uint pin_base, uint32_t bit_rate);
int own_i2c_read_blocking(uint8_t dev_addr, uint8_t *data, size_t data_sz, bool keep_session);
int own_i2c_write_blocking(uint8_t dev_addr, const uint8_t *data, size_t data_sz, bool keep_session);

#endif
