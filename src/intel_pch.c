/* Intel ICH/PCH specifics: chipset identification (LPC bridge 00:1F.0), PIRQ A-H routers
 * (LPC 60h-63h / 68h-6Bh), RCBA and PMBASE, SMI_EN (PMBASE+30h), the $PIR table with Intel's
 * link-byte encoding, and the routers / line commands. Everything else in w9xfix is plain PCI;
 * this is the only file that knows a chipset. */
#include "w9xfix.h"

static const struct { u16 lo, hi; const char *name; int routers; } lpc_ids[] = {
    {0x2410,0x241F,"ICH",1},            {0x2440,0x244F,"ICH2",1},          {0x2480,0x248F,"ICH3",1},
    {0x24C0,0x24CF,"ICH4",1},           {0x24D0,0x24DF,"ICH5",1},          {0x2640,0x264F,"ICH6",1},
    {0x27B0,0x27BF,"ICH7",1},           {0x2810,0x281F,"ICH8",1},          {0x2910,0x291F,"ICH9",1},
    {0x3A10,0x3A1F,"ICH10",1},          {0x3B00,0x3B1F,"5-series PCH",1},  {0x1C40,0x1C5F,"6-series PCH",1},
    {0x1E40,0x1E5F,"7-series PCH",1},   {0x8C40,0x8C5F,"8-series PCH",1},  {0x8CC0,0x8CDF,"9-series PCH",1},
    {0x9C40,0x9C5F,"8-series PCH-LP",1},{0x9CC0,0x9CDF,"9-series PCH-LP",1},
    {0xA140,0xA15F,"100-series PCH (PIRQ routing lives in PCR - routers unsupported)",0},
    {0xA2C0,0xA2DF,"200-series PCH (PCR - routers unsupported)",0},
    {0xA300,0xA31F,"300-series PCH (PCR - routers unsupported)",0},
    {0,0,NULL,0}
};

void chipset_detect(struct chipset *c)
{
    u32 id = cfg_read32(0, 0x1F, 0, 0); int i;
    memset(c, 0, sizeof *c);
    c->vid = (u16)id; c->did = (u16)(id >> 16);
    if (id == 0xFFFFFFFFUL || id == 0) { c->name = "no bridge at 00:1F.0"; return; }
    if (c->vid != 0x8086 || (dev_class(0, 0x1F, 0) >> 8) != 0x0601) {
        c->name = "non-Intel LPC: routers / $PIR link decoding not supported"; return;
    }
    c->intel = 1; c->routers_ok = 1; c->name = "Intel LPC (unknown generation, ICH-style routers assumed)";
    for (i = 0; lpc_ids[i].name; i++)
        if (c->did >= lpc_ids[i].lo && c->did <= lpc_ids[i].hi) { c->name = lpc_ids[i].name; c->routers_ok = lpc_ids[i].routers; break; }
    c->pmbase = cfg_read16(0, 0x1F, 0, 0x40) & 0xFF80;
    c->rcba   = cfg_read32(0, 0x1F, 0, 0xF0) & 0xFFFFC000UL;
}

static u8 router_reg(int i) { return (u8)(i < 4 ? 0x60 + i : 0x68 + i - 4); }
u8 router_get(int i)
{
    u8 v = cfg_read8(0, 0x1F, 0, router_reg(i));
    return (v & 0x80) ? 0 : (v & 0x0F);
}
void router_set(int i, u8 irq) { cfg_write8(0, 0x1F, 0, router_reg(i), irq ? (irq & 0x0F) : 0x80); }

u32 smi_en_read(u16 pmbase) { return pmbase ? io_ind(pmbase + 0x30) : 0; }
void smi_en_decode(u32 v, char *buf)
{
    static const char *bit[32] = { "GBL", "EOS", "BIOS", "LEGACY_USB", "SLP", "APMC", "SWSMI_TMR", "BIOS_RLS",
        0, 0, 0, "MCSMI", 0, "TCO", "PERIODIC", 0, 0, "LEGACY_USB2", "INTEL_USB2", 0, 0, 0, 0, 0,
        0, 0, 0, "GPIO_UNLOCK", 0, 0, 0, 0 };
    int i; *buf = 0;
    for (i = 0; i < 32; i++)
        if ((v >> i) & 1) { if (*buf) strcat(buf, " "); if (bit[i]) strcat(buf, bit[i]); else sprintf(buf + strlen(buf), "b%d", i); }
}

/* ---- $PIR (PCI IRQ routing table, F0000h-FFFFFh): slot -> link byte per INT pin. On Intel
 * the link byte is the router register number (60h-63h, 68h-6Bh). */
u32 pir_base; u16 pir_count;
struct pir_slot { u8 bus, devfn; struct { u8 link; u16 bitmap; } intx[4]; u8 slot; };

void pir_find(void)
{
    u32 a; pir_base = 0;
    for (a = 0xF0000UL; a < 0x100000UL; a += 16)
        if (mem_readb(a) == '$' && mem_readb(a + 1) == 'P' && mem_readb(a + 2) == 'I' && mem_readb(a + 3) == 'R') {
            u16 size = (u16)mem_readb(a + 6) | ((u16)mem_readb(a + 7) << 8);
            if (size >= 32 && ((size - 32) % 16) == 0) { pir_base = a; pir_count = (u16)((size - 32) / 16); return; }
        }
}
static void pir_slot_read(u16 idx, struct pir_slot *s)
{
    u32 a = pir_base + 32 + (u32)idx * 16; int i;
    s->bus = mem_readb(a); s->devfn = mem_readb(a + 1);
    for (i = 0; i < 4; i++) {
        s->intx[i].link   = mem_readb(a + 2 + i * 3);
        s->intx[i].bitmap = (u16)mem_readb(a + 3 + i * 3) | ((u16)mem_readb(a + 4 + i * 3) << 8);
    }
    s->slot = mem_readb(a + 14);
}
static int link_to_router(u8 link)
{
    if (link >= 0x60 && link <= 0x63) return link - 0x60;
    if (link >= 0x68 && link <= 0x6B) return link - 0x68 + 4;
    return -1;
}
int pir_router(u8 bus, u8 dev, u8 pin)
{
    u16 idx; struct pir_slot s;
    if (!pir_base || pin < 1 || pin > 4) return -1;
    for (idx = 0; idx < pir_count; idx++) {
        pir_slot_read(idx, &s);
        if (s.bus == bus && (s.devfn >> 3) == dev) return link_to_router(s.intx[pin - 1].link);
    }
    return -1;
}

/* ---- routers A,B,..,H: set the PIRQ routers, mark their IRQs level in the ELCR (and clear
 * the level bit of an IRQ a router no longer uses) ---------------------------------------- */
int cmd_routers(const char *list)
{
    struct chipset cs; u8 want[8]; int i = 0; u16 elcr; const char *p = list;
    if (!pci_available()) return 1;
    chipset_detect(&cs);
    if (!cs.routers_ok) { out("ROUTER  %s - refusing\n", cs.name); return 1; }
    memset(want, 0, sizeof want);
    while (i < 8) { want[i++] = (u8)atoi(p); p = strchr(p, ','); if (!p) break; p++; }
    elcr = elcr_get();
    for (i = 0; i < 8; i++) { u8 old = router_get(i); if (old > 2 && old != 8 && old != 13) elcr &= (u16)~(1u << old); }
    for (i = 0; i < 8; i++) { router_set(i, want[i]); if (want[i] > 2 && want[i] != 8 && want[i] != 13) elcr |= (u16)(1u << want[i]); }
    elcr_set(elcr);
    out("ROUTER ");
    for (i = 0; i < 8; i++) { u8 v = router_get(i); if (v) out(" %c=%u", 'A' + i, v); else out(" %c=-", 'A' + i); }
    out("  ELCR=%04X\n", elcr_get());
    return 0;
}

/* ---- line: interrupt-line register (3Ch) := router IRQ from the $PIR link -------------- */
struct line_ctx { int changed, checked; };
static void line_one(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct line_ctx *lc = ctx;
    int r = pir_router(bus, dev, cfg_read8(bus, dev, fn, 0x3D));
    u8 want, have;
    if (r < 0) return;
    lc->checked++;
    want = router_get(r); if (!want) want = 0xFF;
    have = cfg_read8(bus, dev, fn, 0x3C);
    if (have != want) {
        out("LINE  %02X:%02X.%u pin %c via PIRQ %c: line %u -> %u\n", bus, dev, fn,
            'A' + cfg_read8(bus, dev, fn, 0x3D) - 1, 'A' + r, have, want);
        cfg_write8(bus, dev, fn, 0x3C, want);
        lc->changed++;
    }
}
int cmd_line(void)
{
    struct chipset cs; struct line_ctx lc; lc.changed = lc.checked = 0;
    if (!pci_available()) return 1;
    chipset_detect(&cs);
    if (!cs.intel) { out("LINE  %s - refusing\n", cs.name); return 1; }
    pir_find();
    if (!pir_base) { out("LINE  no $PIR table found\n"); return 1; }
    foreach_int_device(line_one, &lc);
    out("LINE  %d device(s) checked against $PIR, %d line register(s) rewritten\n", lc.checked, lc.changed);
    return 0;
}
