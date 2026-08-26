/* Host test back end (-DHOSTTEST): a fake PCH-ish config space so the logic can be built and
 * exercised with any C compiler. Fakes: host bridge, LPC (Q67, routers, PMBASE, RCBA), PEG root
 * port -> bus 1, GPU with an asymmetric ASPM link + degraded width, MEI (asserting), EHCI
 * (BIOS-owned). Capability-ID/next-pointer bytes of USBLEGSUP are read-only like hardware. */
#include "w9xfix.h"
#include <time.h>

static u32 cf8;
static u8 fake_host[256], fake_lpc[256], fake_peg[256], fake_gpu[256], fake_mei[256], fake_ehci[256];
static u8 elcr[2];

static u8 *space(u32 addr)
{
    u32 b = (addr >> 16) & 0xFF, d = (addr >> 11) & 0x1F, f = (addr >> 8) & 7;
    if (b == 0 && d == 0x00 && f == 0) return fake_host;
    if (b == 0 && d == 0x01 && f == 0) return fake_peg;
    if (b == 0 && d == 0x16 && f == 0) return fake_mei;
    if (b == 0 && d == 0x1A && f == 0) return fake_ehci;
    if (b == 0 && d == 0x1F && f == 0) return fake_lpc;
    if (b == 1 && d == 0x00 && f == 0) return fake_gpu;
    return NULL;
}
static void write_bytes(u8 *s, unsigned off, const void *v, unsigned n)
{
    u8 keep0 = s[0x68], keep1 = s[0x69];
    memcpy(s + off, v, n);
    if (s == fake_ehci) { s[0x68] = keep0; s[0x69] = keep1; }
}
u32 io_ind(u16 port)
{
    u32 v = 0xFFFFFFFFu; u8 *s;
    if (port == 0xCF8) return cf8;
    if (port == 0xCFC) { s = space(cf8); if (s) memcpy(&v, s + (cf8 & 0xFC), 4); return v; }
    if (port == 0x430) return 0x0002203Bu;                       /* SMI_EN at PMBASE+30h */
    return v;
}
void io_outd(u16 port, u32 v)
{
    u8 *s;
    if (port == 0xCF8) cf8 = v;
    else if (port == 0xCFC) { s = space(cf8); if (s) write_bytes(s, cf8 & 0xFC, &v, 4); }
}
u16 io_inw(u16 port)
{
    u16 w = 0xFFFF; u8 *s;
    if (port >= 0xCFC && port <= 0xCFE) { s = space(cf8); if (s) memcpy(&w, s + ((cf8 & 0xFC) | (port & 2)), 2); }
    return w;
}
void io_outw(u16 port, u16 v)
{
    u8 *s;
    if (port >= 0xCFC && port <= 0xCFE) { s = space(cf8); if (s) write_bytes(s, (cf8 & 0xFC) | (port & 2), &v, 2); }
}
u8 io_inb(u16 port)
{
    u8 *s;
    if (port == 0x4D0) return elcr[0];
    if (port == 0x4D1) return elcr[1];
    if (port == 0x21) return 0x40;                                /* IMR: IRQ 6 masked */
    if (port == 0x20 || port == 0xA0 || port == 0xA1) return 0;
    if (port >= 0xCFC && port <= 0xCFF) { s = space(cf8); return s ? s[(cf8 & 0xFC) | (port & 3)] : 0xFF; }
    return 0xFF;
}
void io_outb(u16 port, u8 v)
{
    u8 *s;
    if (port == 0x4D0) elcr[0] = v;
    else if (port == 0x4D1) elcr[1] = v;
    else if (port >= 0xCFC && port <= 0xCFF) { s = space(cf8); if (s) write_bytes(s, (cf8 & 0xFC) | (port & 3), &v, 1); }
}
u8 mem_readb(u32 phys) { (void)phys; return 0; }                 /* no $PIR, no BIOS date */
void sys_time_str(char *buf)
{
    time_t t = time(NULL); struct tm *tm = localtime(&t);
    sprintf(buf, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
}
int cpu_has_cpuid(void) { return 0; }
int pci_last_bus(void) { return 1; }
void cpuid_raw(u32 leaf, u32 *r) { (void)leaf; r[0] = r[1] = r[2] = r[3] = 0; }

static void id(u8 *s, u16 vid, u16 did, u32 cls)
{
    s[0] = (u8)vid; s[1] = (u8)(vid >> 8); s[2] = (u8)did; s[3] = (u8)(did >> 8);
    s[9] = (u8)cls; s[10] = (u8)(cls >> 8); s[11] = (u8)(cls >> 16);
}
static void put32(u8 *s, unsigned off, u32 v) { memcpy(s + off, &v, 4); }
static void put16(u8 *s, unsigned off, u16 v) { memcpy(s + off, &v, 2); }

void host_init(void)
{
    id(fake_host, 0x8086, 0x0100, 0x060000);
    id(fake_lpc, 0x8086, 0x1C4E, 0x060100);                      /* Q67 */
    fake_lpc[0x0E] = 0x80;
    fake_lpc[0x60] = 11; fake_lpc[0x61] = 10; fake_lpc[0x62] = 5; fake_lpc[0x63] = 3;
    fake_lpc[0x68] = 0x80; fake_lpc[0x69] = 0x80; fake_lpc[0x6A] = 0x80; fake_lpc[0x6B] = 0x80;
    put16(fake_lpc, 0x40, 0x0401);                               /* PMBASE 400h */
    put32(fake_lpc, 0xF0, 0xFED1C001u);                          /* RCBA */
    elcr[0] = 0x28; elcr[1] = 0x0E;
    /* MEI 8086:1C3A class 0780, pin A line 11, asserting (status bit 3) */
    id(fake_mei, 0x8086, 0x1C3A, 0x078000);
    fake_mei[0x3C] = 11; fake_mei[0x3D] = 1; fake_mei[6] = 0x18;
    /* PEG root port: header type 1, secondary bus 1, PCIe cap @A0 type 4 (root port) */
    id(fake_peg, 0x8086, 0x0101, 0x060400);
    fake_peg[0x0E] = 0x01; fake_peg[0x19] = 0x01; fake_peg[0x3C] = 11; fake_peg[0x3D] = 1;
    fake_peg[6] = 0x10; fake_peg[0x34] = 0xA0;
    fake_peg[0xA0] = 0x10; fake_peg[0xA1] = 0; put16(fake_peg, 0xA2, 0x0042);
    put32(fake_peg, 0xAC, 0x02212D02u);                          /* LinkCap: ASPM 3, x16, L0s 128-256ns, L1 16-32us */
    put16(fake_peg, 0xB0, 0x0043); put16(fake_peg, 0xB2, 0x1041); /* LinkCtl ASPM 3; LinkSta x4 (!) */
    /* GPU 1002:5B64: PCIe cap @58, ASPM L0s only (asymmetric), x4 negotiated of x16 */
    id(fake_gpu, 0x1002, 0x5B64, 0x030000);
    fake_gpu[0x3C] = 11; fake_gpu[0x3D] = 1; fake_gpu[6] = 0x10; fake_gpu[0x34] = 0x58;
    put16(fake_gpu, 0x2C, 0x17AF); put16(fake_gpu, 0x2E, 0x3000);
    fake_gpu[0x58] = 0x10; fake_gpu[0x59] = 0; put16(fake_gpu, 0x5A, 0x0002);
    put32(fake_gpu, 0x5C, 0x00000440u);                          /* DevCap: accepts L0s 128ns, L1 4us */
    put32(fake_gpu, 0x64, 0x00001D01u);                          /* LinkCap: ASPM 3, x16, L0s 64-128ns, L1 4-8us */
    put16(fake_gpu, 0x68, 0x0041); put16(fake_gpu, 0x6A, 0x1041);
    /* EHCI 8086:1C2D class 0C0320, BAR0, bus master, USBLEGSUP@68 BIOS-owned, SMIs armed */
    id(fake_ehci, 0x8086, 0x1C2D, 0x0C0320);
    put16(fake_ehci, 4, 0x0006); fake_ehci[0x3C] = 11; fake_ehci[0x3D] = 1;
    put32(fake_ehci, 0x10, 0x80D40000u);
    put32(fake_ehci, 0x68, 0x00010001u); put32(fake_ehci, 0x6C, 0x00002017u);
}
