/* w9xfix - Win9x-on-modern-hardware fixer. Shared declarations.
 * Layout: main.c (options, log, dispatch) | io_dos.c / io_host.c (port I/O, <1 MB reads, CPUID, time)
 * pci.c (config space, iteration, selectors, intx) | pic.c (8259/ELCR) | intel_pch.c (PIRQ routers,
 * $PIR, SMI_EN, chipset id) | aspm.c (PCIe links) | ehci.c (BIOS->OS hand-off) | show.c (show, check) */
#ifndef W9XFIX_H
#define W9XFIX_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define W9XFIX_VERSION "0.4.1"

#ifdef HOSTTEST
typedef unsigned int   u32;
#else
typedef unsigned long  u32;
#endif
typedef unsigned short u16;
typedef unsigned char  u8;
#define UL(x) ((unsigned long)(x))

/* main.c: options and output. Every line goes through out(): stdout unless -q, plus the -l file. */
extern int g_verbose, g_quiet, g_dry;
void out(const char *fmt, ...);

/* io_dos.c / io_host.c. The 32-bit port I/O and CPUID are Watcom inline pragmas: they must be
 * visible in every translation unit, hence here (386+; 32-bit in/out are not in the 16-bit clib). */
#ifndef HOSTTEST
u32 io_ind(u16 port);
#pragma aux io_ind = "in eax,dx" "mov edx,eax" "shr edx,16" parm [dx] value [dx ax] modify [ax dx];
void io_outd(u16 port, u32 v);
#pragma aux io_outd = "shl ecx,16" "mov cx,ax" "mov eax,ecx" "out dx,eax" parm [dx] [cx ax] modify [ax cx];
/* EFLAGS.ID (bit 21) toggles only on CPUs that have CPUID (late 486+). */
int cpu_has_cpuid(void);
#pragma aux cpu_has_cpuid = \
    "pushfd" "pop eax" "mov ecx,eax" "xor eax,200000h" "push eax" "popfd" \
    "pushfd" "pop eax" "push ecx" "popfd" "xor eax,ecx" "shr eax,21" "and ax,1" \
    value [ax] modify [ax cx];
void cpuid_raw(u32 leaf, u32 *r);   /* r[0..3] = eax ebx ecx edx */
#pragma aux cpuid_raw = \
    "shl edx,16" "mov dx,ax" "mov eax,edx" "xor ecx,ecx" \
    0x0F 0xA2 \
    "mov [si],eax" "mov [si+4],ebx" "mov [si+8],ecx" "mov [si+12],edx" \
    parm [dx ax] [si] modify [ax bx cx dx si];
#else
u32  io_ind(u16 port);
void io_outd(u16 port, u32 v);
int  cpu_has_cpuid(void);
void cpuid_raw(u32 leaf, u32 *r);
#endif
u16  io_inw(u16 port);
void io_outw(u16 port, u16 v);
u8   io_inb(u16 port);
void io_outb(u16 port, u8 v);
u8   mem_readb(u32 phys);            /* physical address below 1 MB (BIOS area) */
void sys_time_str(char *buf);        /* "hh:mm:ss" */
int  pci_last_bus(void);             /* PCI BIOS INT 1Ah B101h; 255 if unavailable */
#ifdef HOSTTEST
void host_init(void);
#endif

/* pci.c - configuration mechanism #1. Writes honour -n (dry-run) and print a DRY line instead. */
int  pci_available(void);
u32  cfg_read32(u8 bus, u8 dev, u8 fn, u8 reg);
u16  cfg_read16(u8 bus, u8 dev, u8 fn, u8 reg);
u8   cfg_read8 (u8 bus, u8 dev, u8 fn, u8 reg);
void cfg_write32(u8 bus, u8 dev, u8 fn, u8 reg, u32 v);
void cfg_write16(u8 bus, u8 dev, u8 fn, u8 reg, u16 v);
void cfg_write8 (u8 bus, u8 dev, u8 fn, u8 reg, u8 v);
int  dev_present(u8 bus, u8 dev, u8 fn);
u8   find_cap(u8 bus, u8 dev, u8 fn, u8 id);      /* standard capability list; 0 = none */
u32  dev_class(u8 bus, u8 dev, u8 fn);            /* 24-bit class code */
const char *class_name(u32 cls);
int  class_driverless(u32 cls, u16 vid);           /* classes no Win9x driver ever binds */
typedef void (*dev_cb)(u8 bus, u8 dev, u8 fn, void *ctx);
void foreach_function(dev_cb cb, void *ctx);      /* every function on every bus */
void foreach_int_device(dev_cb cb, void *ctx);    /* only functions with an INT pin */

/* device selector: BB:DD.F | VVVV:DDDD | class:CCCC | class:CCCCPP */
#define SEL_BDF   1
#define SEL_VD    2
#define SEL_CLASS 3
struct sel { int kind; u8 bus, dev, fn; u16 vid, did; u32 cls, clsmask; char text[16]; };
int  sel_parse(const char *s, struct sel *sel);
int  sel_match(const struct sel *sel, u8 bus, u8 dev, u8 fn);
int  cmd_intx(int argc, char **argv);

/* pic.c */
u16  elcr_get(void);
void elcr_set(u16 v);
u16  pic_irr(void);
u16  pic_isr(void);
u16  pic_imr(void);

/* intel_pch.c */
struct chipset { int intel, routers_ok; u16 vid, did; const char *name; u32 rcba; u16 pmbase; };
void chipset_detect(struct chipset *c);
u8   router_get(int i);                 /* PIRQ A..H -> IRQ, 0 = disabled */
void router_set(int i, u8 irq);
u32  smi_en_read(u16 pmbase);
void smi_en_decode(u32 v, char *buf);
extern u32 pir_base;
extern u16 pir_count;
void pir_find(void);
int  pir_router(u8 bus, u8 dev, u8 pin); /* router index 0..7 for a device pin, -1 = no $PIR entry */
int  cmd_routers(const char *list);
int  cmd_line(void);

/* aspm.c */
struct link_stats { int links, on, mismatch, fixed, degraded, latency; };
void aspm_walk_links(int set, u8 val, int quiet, struct link_stats *st);
int  cmd_aspm(int argc, char **argv);

/* ehci.c */
struct ehci_stats { int n, bios_owned, smi_on, fail; };
void ehci_walk(int handoff, u8 eecp, const struct sel *only, int quiet, struct ehci_stats *st);
int  cmd_ehci(int argc, char **argv);

/* show.c */
int  cmd_show(void);
int  cmd_check(int argc, char **argv);

#endif
