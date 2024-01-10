#include "interrupts.h"
#include "own_i2c.h"

#include <hardware/dma.h>
#include <hardware/irq.h>
#include <hardware/pio.h>

//! \b DMA_IRQ_0 global handler.
static void dma_irq0() {
	own_i2c_dma_irq0_handler(&dma_hw->ints0);
}

//! \b PIO0_IRQ_0 global handler.
static void pio0_irq0() {
	own_i2c_pio0_irq0_handler();
}

//! Setups interrupts used in entire system.
//! @return Always true.
bool setup_interrupts() {
	bool result = true;
	
	irq_set_exclusive_handler(DMA_IRQ_0, dma_irq0);
	irq_set_enabled(DMA_IRQ_0, true);
	irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq0);
	irq_set_enabled(PIO0_IRQ_0, true);
	
	return result;
}
