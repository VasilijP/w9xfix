/* 8259 PIC pair: ELCR (level/edge per IRQ, ports 4D0h/4D1h) and the IRR/ISR/IMR snapshot. */
#include "w9xfix.h"

u16 elcr_get(void) { return (u16)io_inb(0x4D0) | ((u16)io_inb(0x4D1) << 8); }
void elcr_set(u16 v)
{
    if (g_dry) { out("DRY   ELCR <- %04X (not written)\n", v); return; }
    io_outb(0x4D0, (u8)v); io_outb(0x4D1, (u8)(v >> 8));
}
/* OCW3 selects IRR (0Ah) or ISR (0Bh) for the next read of the command port. */
u16 pic_irr(void) { io_outb(0x20, 0x0A); io_outb(0xA0, 0x0A); return (u16)io_inb(0x20) | ((u16)io_inb(0xA0) << 8); }
u16 pic_isr(void) { io_outb(0x20, 0x0B); io_outb(0xA0, 0x0B); return (u16)io_inb(0x20) | ((u16)io_inb(0xA0) << 8); }
u16 pic_imr(void) { return (u16)io_inb(0x21) | ((u16)io_inb(0xA1) << 8); }
