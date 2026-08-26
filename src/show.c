/* show: everything a boot log should carry - platform (BIOS date, CPU), chipset, SMI_EN, PIC
 * state, PIRQ routers, $PIR, and one line per PCI function with the clues that matter in Win9x
 * (INT pin/line vs $PIR, INTx off, asserting now, driverless class, MSI, status error bits).
 * check: verify the fixed state and print one RESULT line with a batch-usable errorlevel. */
#include "w9xfix.h"

struct show_ctx { struct chipset cs; int n; };

static void status_errs(u16 sts, char *buf)
{
    *buf = 0;
    if (sts & 0x0100) strcat(buf, " MDPERR");    /* master data parity error */
    if (sts & 0x0800) strcat(buf, " STA");       /* signaled target abort */
    if (sts & 0x1000) strcat(buf, " RTA");       /* received target abort */
    if (sts & 0x2000) strcat(buf, " RMA");       /* received master abort */
    if (sts & 0x4000) strcat(buf, " SERR");
    if (sts & 0x8000) strcat(buf, " DPE");       /* detected parity error */
}
static void show_dev(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct show_ctx *c = ctx;
    u32 id = cfg_read32(bus, dev, fn, 0), cls = dev_class(bus, dev, fn);
    u16 cmd = cfg_read16(bus, dev, fn, 4), sts = cfg_read16(bus, dev, fn, 6);
    u8 hdr = cfg_read8(bus, dev, fn, 0x0E) & 0x7F, pin = cfg_read8(bus, dev, fn, 0x3D), line = cfg_read8(bus, dev, fn, 0x3C);
    u8 msi = find_cap(bus, dev, fn, 0x05);
    int drv = class_driverless(cls, (u16)id);
    char eb[40];
    c->n++;
    out("DEV   %02X:%02X.%u %04X:%04X %04X %-10s", bus, dev, fn, (unsigned)(id & 0xFFFF), (unsigned)(id >> 16),
        (unsigned)(cls >> 8), class_name(cls));
    if (hdr == 1) out(" -> bus %02X", cfg_read8(bus, dev, fn, 0x19));
    if (pin >= 1 && pin <= 4) {
        int r = c->cs.intel ? pir_router(bus, dev, pin) : -1;
        out("  pin %c line %-3u", 'A' + pin - 1, line);
        if (r >= 0) { u8 want = router_get(r); out(" $PIR %c=%u%s", 'A' + r, want, (want && want != line) ? " != line" : ""); }
        else if (pir_base) out(" $PIR -");
    } else out("  no INT pin");
    if (cmd & 0x400) out(" [INTx off]");
    if (sts & 0x008) out(" [ASSERTING]");
    if (drv) out((cmd & 0x400) ? " [driverless class]" : " [driverless class, INTx ON]");
    if (msi && (cfg_read16(bus, dev, fn, (u8)(msi + 2)) & 1)) out(" [MSI enabled]");
    status_errs(sts, eb);
    if (*eb) out(" [status:%s]", eb);
    out("\n");
    if (g_verbose) {
        out("      cmd=%04X sts=%04X hdr=%02X rev=%02X", cmd, sts, hdr, cfg_read8(bus, dev, fn, 8));
        if (hdr == 0) out(" subsys=%04X:%04X", cfg_read16(bus, dev, fn, 0x2C), cfg_read16(bus, dev, fn, 0x2E));
        if (hdr == 1) out(" buses %02X-%02X-%02X bridgectl=%04X", cfg_read8(bus, dev, fn, 0x18), cfg_read8(bus, dev, fn, 0x19),
                          cfg_read8(bus, dev, fn, 0x1A), cfg_read16(bus, dev, fn, 0x3E));
        if (find_cap(bus, dev, fn, 0x10)) out(" pcie@%02X", find_cap(bus, dev, fn, 0x10));
        if (msi) out(" msi@%02X", msi);
        if (find_cap(bus, dev, fn, 0x01)) out(" pm@%02X", find_cap(bus, dev, fn, 0x01));
        out("\n");
    }
}

static void bios_date(char *buf)
{
    int i;
    for (i = 0; i < 8; i++) { u8 ch = mem_readb(0xFFFF5UL + i); buf[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '?'; }
    buf[8] = 0;
    if (buf[0] == '?' || buf[0] == 0) strcpy(buf, "n/a");
}
int cmd_show(void)
{
    struct show_ctx c; char buf[160]; u16 elcr; int i;
    if (!pci_available()) return 1;
    memset(&c, 0, sizeof c);
    chipset_detect(&c.cs);
    bios_date(buf);
    out("BIOS  date %s", buf);
    if (cpu_has_cpuid()) {
        u32 r[4]; char vend[13];
        cpuid_raw(0, r); memcpy(vend, &r[1], 4); memcpy(vend + 4, &r[3], 4); memcpy(vend + 8, &r[2], 4); vend[12] = 0;
        cpuid_raw(1, r);
        out("  CPU %s signature %08lX (family %lu model %lX)", vend, UL(r[0]),
            UL(((r[0] >> 8) & 0xF) + (((r[0] >> 8) & 0xF) == 0xF ? ((r[0] >> 20) & 0xFF) : 0)),
            UL(((r[0] >> 4) & 0xF) | (((r[0] >> 8) & 0xF) >= 6 ? ((r[0] >> 12) & 0xF0) : 0)));
    } else out("  CPU no CPUID");
    out("\n");
    out("CHIP  LPC 00:1F.0 %04X:%04X %s", c.cs.vid, c.cs.did, c.cs.name);
    if (c.cs.intel) out("  RCBA %08lX  PMBASE %04X", UL(c.cs.rcba), c.cs.pmbase);
    out("\n");
    if (c.cs.intel && c.cs.pmbase) {
        u32 smi = smi_en_read(c.cs.pmbase);
        smi_en_decode(smi, buf);
        out("SMI   SMI_EN=%08lX [%s]%s\n", UL(smi), buf, (smi & 0x00020008UL) ? "  (BIOS USB emulation SMIs armed)" : "");
    }
    elcr = elcr_get();
    out("PIC   IRR=%04X ISR=%04X IMR=%04X ELCR=%04X level:", pic_irr(), pic_isr(), pic_imr(), elcr);
    for (i = 0; i < 16; i++) if ((elcr >> i) & 1) out(" %d", i);
    out("\n");
    if (c.cs.intel) {
        out("ROUTER");
        for (i = 0; i < 8; i++) { u8 v = router_get(i); if (v) out(" %c=%u", 'A' + i, v); else out(" %c=-", 'A' + i); }
        out("%s\n", c.cs.routers_ok ? "" : "  (not meaningful on this chipset)");
    }
    pir_find();
    if (pir_base) out("PIR   table at %05lX, %u slots\n", UL(pir_base), pir_count); else out("PIR   no $PIR table\n");
    foreach_function(show_dev, &c);
    out("SHOW  %d PCI function(s)\n", c.n);
    return 0;
}

/* ---- check [SEL ...] ------------------------------------------------------------------- */
struct chk_ctx { int fail, warn; int nlist; struct sel list[8]; int found[8]; };

static void chk_intx(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct chk_ctx *c = ctx; int i;
    u32 id = cfg_read32(bus, dev, fn, 0); u16 cmd = cfg_read16(bus, dev, fn, 4), sts = cfg_read16(bus, dev, fn, 6);
    u8 pin = cfg_read8(bus, dev, fn, 0x3D);
    if (pin >= 1 && pin <= 4 && class_driverless(dev_class(bus, dev, fn), (u16)id)) {
        out("CHECK intx driverless %02X:%02X.%u %04X:%04X %s: INTx %s -> %s\n", bus, dev, fn,
            (unsigned)(id & 0xFFFF), (unsigned)(id >> 16), class_name(dev_class(bus, dev, fn)),
            (cmd & 0x400) ? "off" : "ON", (cmd & 0x400) ? "PASS" : "FAIL");
        if (!(cmd & 0x400)) c->fail++;
    }
    for (i = 0; i < c->nlist; i++)
        if (sel_match(&c->list[i], bus, dev, fn)) {
            c->found[i]++;
            out("CHECK intx listed %02X:%02X.%u (%s): INTx %s -> %s\n", bus, dev, fn, c->list[i].text,
                (cmd & 0x400) ? "off" : "ON", (cmd & 0x400) ? "PASS" : "FAIL");
            if (!(cmd & 0x400)) c->fail++;
        }
    if (pin >= 1 && pin <= 4 && (sts & 0x008)) {
        out("CHECK asserting %02X:%02X.%u %04X:%04X INTx asserting now -> FAIL\n", bus, dev, fn,
            (unsigned)(id & 0xFFFF), (unsigned)(id >> 16));
        c->fail++;
    }
}
int cmd_check(int argc, char **argv)
{
    struct chk_ctx c; struct link_stats ls; struct ehci_stats es; int i;
    memset(&c, 0, sizeof c);
    for (i = 2; i < argc && c.nlist < 8; i++)
        if (!sel_parse(argv[i], &c.list[c.nlist++])) return 2;
    if (!pci_available()) return 1;
    foreach_function(chk_intx, &c);
    for (i = 0; i < c.nlist; i++)
        if (!c.found[i]) { out("CHECK intx listed %s: not present (disabled in BIOS?) -> WARN\n", c.list[i].text); c.warn++; }
    aspm_walk_links(0, 0, 1, &ls);
    out("CHECK aspm %d link(s): %d on, %d mismatched -> %s\n", ls.links, ls.on, ls.mismatch, (ls.on || ls.mismatch) ? "FAIL" : "PASS");
    if (ls.on || ls.mismatch) c.fail++;
    if (ls.degraded) { out("CHECK aspm %d link(s) training-degraded -> WARN\n", ls.degraded); c.warn++; }
    ehci_walk(0, 0x68, NULL, 1, &es);
    if (es.n) {
        out("CHECK ehci %d controller(s): %d BIOS-owned, %d with SMIs armed -> %s\n", es.n, es.bios_owned, es.smi_on,
            (es.bios_owned || es.smi_on) ? "WARN (Win9x USB will run on timeouts)" : "PASS");
        if (es.bios_owned || es.smi_on) c.warn++;
    } else out("CHECK ehci no controller (nothing to hand off)\n");
    if (c.fail) out("RESULT FAIL (%d failure(s), %d warning(s))\n", c.fail, c.warn);
    else out("RESULT PASS (%d warning(s))\n", c.warn);
    return c.fail ? 1 : 0;
}
