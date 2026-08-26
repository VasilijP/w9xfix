/* PCIe links: ASPM on BOTH ends. The spec wants identical ASPM programming on both ends of a
 * link and in every function of a device; a BIOS that leaves L0s/L1 on - or on at one end only -
 * hangs early PCIe GPUs under Win9x (no PCIe-aware driver reconciles the ends). The Optiplex 990
 * shipped its PEG link L0s+L1 on both ends and the GPU's second function was still on after the
 * first fix: 3DMark2000 hard lockup. Cure = symmetric OFF on every link and function, before WIN.
 * Upstream ends = root ports (port type 4) and switch downstream ports (6); the downstream end =
 * every PCIe function on the port's secondary bus. Disable device-first, enable port-first.
 * Also reported: negotiated vs. maximum link width (a damaged card trains at x1/x4 - seen in the
 * lab) and whether the device's acceptable L0s/L1 latency covers the exit latencies of the link. */
#include "w9xfix.h"

static u8  pcie_type(u8 b, u8 d, u8 f, u8 cap) { return (u8)((cfg_read16(b, d, f, (u8)(cap + 2)) >> 4) & 0x0F); }
static u32 link_cap (u8 b, u8 d, u8 f, u8 cap) { return cfg_read32(b, d, f, (u8)(cap + 0x0C)); }
static u16 link_ctl (u8 b, u8 d, u8 f, u8 cap) { return cfg_read16(b, d, f, (u8)(cap + 0x10)); }
static u16 link_sta (u8 b, u8 d, u8 f, u8 cap) { return cfg_read16(b, d, f, (u8)(cap + 0x12)); }
static u32 dev_cap  (u8 b, u8 d, u8 f, u8 cap) { return cfg_read32(b, d, f, (u8)(cap + 0x04)); }
static void aspm_set(u8 b, u8 d, u8 f, u8 cap, u8 v)
{
    u16 ctl = link_ctl(b, d, f, cap);
    cfg_write16(b, d, f, (u8)(cap + 0x10), (u16)((ctl & ~3u) | (v & 3)));
}
static const char *lat_l0s[8] = { "<64ns", "64-128ns", "128-256ns", "256-512ns", "512ns-1us", "1-2us", "2-4us", ">4us" };
static const char *lat_l1[8]  = { "<1us", "1-2us", "2-4us", "4-8us", "8-16us", "16-32us", "32-64us", ">64us" };
static const char *acc_l0s[8] = { "64ns", "128ns", "256ns", "512ns", "1us", "2us", "4us", "no limit" };
static const char *acc_l1[8]  = { "1us", "2us", "4us", "8us", "16us", "32us", "64us", "no limit" };
static const char *speed_str(unsigned s)
{
    return s == 1 ? "2.5GT/s" : s == 2 ? "5GT/s" : s == 3 ? "8GT/s" : s == 4 ? "16GT/s" : "?GT/s";
}

struct link_ctx { int set, quiet; u8 val; struct link_stats st; };

static void link_pair(u8 ub, u8 ud, u8 uf, u8 ucap, u8 db, u8 dd, u8 df, u8 dcap, struct link_ctx *c)
{
    u32 id = cfg_read32(db, dd, df, 0);
    u32 ucv = link_cap(ub, ud, uf, ucap), dcv = link_cap(db, dd, df, dcap), ddc = dev_cap(db, dd, df, dcap);
    u16 dsta = link_sta(db, dd, df, dcap), usta = link_sta(ub, ud, uf, ucap);
    u8 up = (u8)(link_ctl(ub, ud, uf, ucap) & 3), down = (u8)(link_ctl(db, dd, df, dcap) & 3);
    unsigned width = (dsta >> 4) & 0x3F, maxw = (dcv >> 4) & 0x3F, umaxw = (ucv >> 4) & 0x3F;
    unsigned l0s_u = (ucv >> 12) & 7, l0s_d = (dcv >> 12) & 7, l1_u = (ucv >> 15) & 7, l1_d = (dcv >> 15) & 7;
    unsigned a_l0s = (ddc >> 6) & 7, a_l1 = (ddc >> 9) & 7;
    int mism = up != down, on = (up | down) != 0, latbad = 0;
    int degraded = width && width < maxw && width < umaxw;
    if (((dcv >> 10) & 1) && (l0s_u > l0s_d ? l0s_u : l0s_d) > a_l0s) latbad |= 1;
    if (((dcv >> 11) & 1) && (l1_u > l1_d ? l1_u : l1_d) > a_l1) latbad |= 2;
    c->st.links++;
    if (on) c->st.on++;
    if (mism) c->st.mismatch++;
    if (degraded) c->st.degraded++;
    if (latbad) c->st.latency++;
    if (!c->quiet) {
        out("LINK  %02X:%02X.%u -> %02X:%02X.%u %04X:%04X  ASPM port %u/%u dev %u/%u%s%s",
            ub, ud, uf, db, dd, df, (unsigned)(id & 0xFFFF), (unsigned)(id >> 16),
            up, (unsigned)((ucv >> 10) & 3), down, (unsigned)((dcv >> 10) & 3),
            mism ? " [MISMATCH]" : "", on ? " [ON]" : " [off]");
        out("  x%u of x%u %s%s%s%s\n", width, maxw, speed_str(dsta & 0xF),
            degraded ? " [TRAINING DEGRADED]" : "", (latbad & 1) ? " [L0s latency unsafe]" : "",
            (latbad & 2) ? " [L1 latency unsafe]" : "");
        if (g_verbose)
            out("      L0s exit port %s dev %s, dev accepts %s | L1 exit port %s dev %s, dev accepts %s | sta port %04X dev %04X\n",
                lat_l0s[l0s_u], lat_l0s[l0s_d], acc_l0s[a_l0s], lat_l1[l1_u], lat_l1[l1_d], acc_l1[a_l1], usta, dsta);
    }
    if (c->set && (up != c->val || down != c->val)) {
        if (c->val) { aspm_set(ub, ud, uf, ucap, c->val); aspm_set(db, dd, df, dcap, c->val); }
        else        { aspm_set(db, dd, df, dcap, 0);      aspm_set(ub, ud, uf, ucap, 0); }
        up = (u8)(link_ctl(ub, ud, uf, ucap) & 3); down = (u8)(link_ctl(db, dd, df, dcap) & 3);
        c->st.fixed++;
        if (!c->quiet) out("      => port %u dev %u%s\n", up, down,
                           (up != c->val || down != c->val) ? (g_dry ? "  (dry-run)" : "  !! did not take") : "");
    }
}
static void port_one(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct link_ctx *c = ctx;
    u8 cap, t, sec, d2, f2, maxfn, n = 0;
    if ((cfg_read8(bus, dev, fn, 0x0E) & 0x7F) != 1) return;       /* bridge header only */
    cap = find_cap(bus, dev, fn, 0x10);
    if (!cap) return;
    t = pcie_type(bus, dev, fn, cap);
    if (t != 4 && t != 6) return;
    sec = cfg_read8(bus, dev, fn, 0x19);
    for (d2 = 0; d2 < 32; d2++) {
        if (!dev_present(sec, d2, 0)) continue;
        maxfn = (cfg_read8(sec, d2, 0, 0x0E) & 0x80) ? 8 : 1;
        for (f2 = 0; f2 < maxfn; f2++) {
            u8 cap2;
            if (!dev_present(sec, d2, f2)) continue;
            cap2 = find_cap(sec, d2, f2, 0x10);
            if (!cap2) continue;
            n++;
            link_pair(bus, dev, fn, cap, sec, d2, f2, cap2, c);
        }
    }
    if (!n && !c->quiet)
        out("LINK  %02X:%02X.%u port ASPM %u/%u -> no device on bus %02X\n", bus, dev, fn,
            (unsigned)(link_ctl(bus, dev, fn, cap) & 3), (unsigned)((link_cap(bus, dev, fn, cap) >> 10) & 3), sec);
}
void aspm_walk_links(int set, u8 val, int quiet, struct link_stats *st)
{
    struct link_ctx c;
    memset(&c, 0, sizeof c); c.set = set; c.val = val; c.quiet = quiet;
    foreach_function(port_one, &c);
    *st = c.st;
}

struct one_ctx { struct sel sel; const char *val; int n; };
static void aspm_one(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct one_ctx *c = ctx; u8 cap; u16 ctl;
    if (!sel_match(&c->sel, bus, dev, fn)) return;
    c->n++;
    cap = find_cap(bus, dev, fn, 0x10);
    if (!cap) { out("ASPM  %02X:%02X.%u no PCIe capability\n", bus, dev, fn); return; }
    ctl = link_ctl(bus, dev, fn, cap);
    out("ASPM  %02X:%02X.%u cap@%02X LinkCtl=%04X (ASPM %u of %u)", bus, dev, fn, cap, ctl, ctl & 3,
        (unsigned)((link_cap(bus, dev, fn, cap) >> 10) & 3));
    if (c->val) {
        aspm_set(bus, dev, fn, cap, (u8)(atoi(c->val) & 3));
        ctl = link_ctl(bus, dev, fn, cap);
        out(" -> %04X (ASPM %u)", ctl, ctl & 3);
    }
    out("\n");
}
int cmd_aspm(int argc, char **argv)
{
    struct link_stats st;
    if (!pci_available()) return 1;
    if (argc == 2 || (argc >= 3 && !strcmp(argv[2], "links"))) {
        int set = argc >= 4; u8 val = set ? (u8)(atoi(argv[3]) & 3) : 0;
        aspm_walk_links(set, val, 0, &st);
        out("ASPM  %d link(s): %d with ASPM on, %d mismatched, %d training-degraded, %d latency-unsafe%s\n",
            st.links, st.on, st.mismatch, st.degraded, st.latency, (!set && (st.on || st.mismatch)) ? "  (fix: w9xfix aspm links 0)" : "");
        if (set) out("ASPM  %d link(s) set to ASPM %u\n", st.fixed, val);
        return 0;
    }
    {
        struct one_ctx c; memset(&c, 0, sizeof c);
        if (!sel_parse(argv[2], &c.sel)) return 2;
        c.val = argc >= 4 ? argv[3] : NULL;
        foreach_function(aspm_one, &c);
        if (!c.n) { out("ASPM  no device matches %s\n", c.sel.text); return 1; }
        return 0;
    }
}
