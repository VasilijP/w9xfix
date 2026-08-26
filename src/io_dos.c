/* Real-mode DOS back end (Open Watcom, 16-bit small model, 386+): 8/16-bit port I/O, reads
 * below 1 MB, DOS time. The 32-bit port I/O and CPUID pragmas live in w9xfix.h (inline). */
#include "w9xfix.h"
#include <conio.h>
#include <i86.h>
#include <dos.h>

u16  io_inw(u16 port)          { return inpw(port); }
void io_outw(u16 port, u16 v)  { outpw(port, v); }
u8   io_inb(u16 port)          { return (u8)inp(port); }
void io_outb(u16 port, u8 v)   { outp(port, v); }

u8 mem_readb(u32 phys)
{
    return *(u8 __far *)MK_FP((u16)(phys >> 4), (u16)(phys & 0xF));
}

int pci_last_bus(void)
{
    union REGS r;
    r.w.ax = 0xB101;
    int86(0x1A, &r, &r);
    return (r.h.ah == 0 && r.w.dx == 0x4350) ? r.h.cl : 255;     /* "PC" of the "PCI " signature in DX */
}

void sys_time_str(char *buf)
{
    struct dostime_t t;
    _dos_gettime(&t);
    sprintf(buf, "%02u:%02u:%02u", t.hour, t.minute, t.second);
}
