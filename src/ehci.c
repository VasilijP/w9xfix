/* EHCI USB legacy-support hand-off, PCI config space only. The "USB Legacy Support" extended
 * capability (ID 01h) sits in CONFIG space at EECP = HCCPARAMS[15:8]; HCCPARAMS itself is MMIO
 * (BAR0 above 1 MB, out of reach in real mode without unreal mode :), so EECP is assumed 68h -
 * Intel ICH/PCH and nearly everyone else - and verified by its capability ID (eecp=XX overrides).
 *   USBLEGSUP    (EECP+0): bit 16 HC BIOS-owned semaphore, bit 24 HC OS-owned semaphore
 *   USBLEGCTLSTS (EECP+4): SMI enables (bits 0-5, 13-15), their status bits above
 * Hand-off = USBLEGCTLSTS := 0, then USBLEGSUP := 01000000h (OS-owned set, BIOS-owned cleared),
 * poll - the EHCIQ H / Linux forced-hand-off order, proven on the Optiplex 990 (BIOS A24).
 * Without it the Win9x USB stack (NUSB) never gets the controllers: interrupts are eaten as
 * SMIs, every transfer completes by timeout -> 1 update/s input, ~100 s boots. The BIOS keyboard
 * emulation dies at the hand-off: run it as the LAST thing before WIN.
 * Healthy handover should take about (or less than) 1 second.
 * */
#include "w9xfix.h"

static const char *own_str(u32 sup)
{
    if (sup & 0x01000000UL) return (sup & 0x00010000UL) ? "[BIOS+OS-owned]" : "[OS-owned]";
    return (sup & 0x00010000UL) ? "[BIOS-owned]" : "[unowned]";
}
static void legctl_str(u32 v, char *buf)
{
    static const char *en[16] = { "USB", "ERR", "PORTCHG", "FLR", "HSE", "AA", 0, 0, 0, 0, 0, 0, 0, "OSOWN", "PCICMD", "BAR" };
    int i; strcpy(buf, "[");
    for (i = 0; i < 16; i++) if (en[i] && ((v >> i) & 1)) { if (buf[1]) strcat(buf, " "); strcat(buf, en[i]); }
    strcat(buf, "]");
}

struct ehci_ctx { int handoff, quiet; u8 eecp; const struct sel *only; struct ehci_stats st; };

static void ehci_one(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct ehci_ctx *c = ctx;
    u32 id, sup, ctl; char eb[64];
    if (dev_class(bus, dev, fn) != 0x0C0320UL) return;
    if (c->only && !sel_match(c->only, bus, dev, fn)) return;
    id  = cfg_read32(bus, dev, fn, 0);
    sup = cfg_read32(bus, dev, fn, c->eecp);
    ctl = cfg_read32(bus, dev, fn, (u8)(c->eecp + 4));
    c->st.n++;
    if (sup & 0x00010000UL) c->st.bios_owned++;
    if (ctl & 0xE03F) c->st.smi_on++;
    if (!c->quiet) {
        legctl_str(ctl, eb);
        out("EHCI  %02X:%02X.%u %04X:%04X  BAR0 %08lX cmd=%04X  LEGSUP@%02X=%08lX %s  LEGCTL=%08lX %s\n",
            bus, dev, fn, (unsigned)(id & 0xFFFF), (unsigned)(id >> 16),
            UL(cfg_read32(bus, dev, fn, 0x10) & 0xFFFFFFF0UL), cfg_read16(bus, dev, fn, 4),
            c->eecp, UL(sup), own_str(sup), UL(ctl), eb);
    }
    /* capability ID 01h AND the reserved bits (23:17, 31:25) zero - a bare ID check is fooled by
     * whatever happens to sit at a wrong offset (seen: C9C25801 at 50h) */
    if ((sup & 0xFF) != 0x01 || (sup & 0xFEFE0000UL)) {
        if (!c->quiet) out("      no USB legacy-support capability at %02X - skipped (try eecp=XX)\n", c->eecp);
        c->st.fail++;
        return;
    }
    if (!c->handoff) return;
    if (!(sup & 0x00010000UL) && (sup & 0x01000000UL) && !(ctl & 0xE03F)) {
        if (!c->quiet) out("      already OS-owned with SMIs off - nothing to do\n");
        return;
    }
    {
        long i;
        cfg_write32(bus, dev, fn, (u8)(c->eecp + 4), 0);            /* SMI enables off */
        cfg_write32(bus, dev, fn, c->eecp, 0x01000000UL);           /* OS-owned, !BIOS-owned */
        for (i = 0; i < 50000L; i++) {                             /* let the SMM handler settle */
            sup = cfg_read32(bus, dev, fn, c->eecp);
            if (!(sup & 0x00010000UL)) break;
        }
        ctl = cfg_read32(bus, dev, fn, (u8)(c->eecp + 4));
        legctl_str(ctl, eb);
        if (!c->quiet) out("      -> hand-off: LEGSUP=%08lX %s  LEGCTL=%08lX %s  (BIOS USB emulation is OFF now)\n",
                           UL(sup), own_str(sup), UL(ctl), eb);
        if (g_dry) { if (!c->quiet) out("      (dry-run: not verified)\n"); }
        else if ((sup & 0x00010000UL) || !(sup & 0x01000000UL) || (ctl & 0xE03F)) {
            if (!c->quiet) out("      !! BIOS did not release the controller\n");
            c->st.fail++;
        }
    }
}
void ehci_walk(int handoff, u8 eecp, const struct sel *only, int quiet, struct ehci_stats *st)
{
    struct ehci_ctx c;
    memset(&c, 0, sizeof c); c.handoff = handoff; c.eecp = eecp; c.only = only; c.quiet = quiet;
    foreach_function(ehci_one, &c);
    *st = c.st;
}
int cmd_ehci(int argc, char **argv)
{
    struct sel sel; struct ehci_stats st; int i, handoff = 0, only = 0; u8 eecp = 0x68;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "handoff")) handoff = 1;
        else if (!strncmp(argv[i], "eecp=", 5)) {
            unsigned v = 0;
            if (sscanf(argv[i] + 5, "%x", &v) != 1 || v < 0x40 || v > 0xF8) return 2;
            eecp = (u8)(v & 0xFC);
        }
        else if (sel_parse(argv[i], &sel)) only = 1;
        else return 2;
    }
    if (!pci_available()) return 1;
    ehci_walk(handoff, eecp, only ? &sel : NULL, 0, &st);
    if (!st.n) { out("EHCI  no EHCI controller found\n"); return 1; }
    return st.fail ? 1 : 0;
}
