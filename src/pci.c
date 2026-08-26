/* PCI configuration mechanism #1 (CF8h/CFCh), capability walk, device iteration, device
 * selectors, class names, and the intx command. All writes honour -n. */
#include "w9xfix.h"

static u32 cfg_addr(u8 bus, u8 dev, u8 fn, u8 reg)
{
    return 0x80000000UL | ((u32)bus << 16) | ((u32)dev << 11) | ((u32)fn << 8) | (reg & 0xFC);
}
u32 cfg_read32(u8 bus, u8 dev, u8 fn, u8 reg) { io_outd(0xCF8, cfg_addr(bus, dev, fn, reg)); return io_ind(0xCFC); }
u16 cfg_read16(u8 bus, u8 dev, u8 fn, u8 reg) { io_outd(0xCF8, cfg_addr(bus, dev, fn, reg)); return io_inw(0xCFC + (reg & 2)); }
u8  cfg_read8 (u8 bus, u8 dev, u8 fn, u8 reg) { io_outd(0xCF8, cfg_addr(bus, dev, fn, reg)); return io_inb(0xCFC + (reg & 3)); }

void cfg_write32(u8 bus, u8 dev, u8 fn, u8 reg, u32 v)
{
    if (g_dry) { out("DRY   %02X:%02X.%u cfg[%02X] <- %08lX (not written)\n", bus, dev, fn, reg, UL(v)); return; }
    io_outd(0xCF8, cfg_addr(bus, dev, fn, reg)); io_outd(0xCFC, v);
}
void cfg_write16(u8 bus, u8 dev, u8 fn, u8 reg, u16 v)
{
    if (g_dry) { out("DRY   %02X:%02X.%u cfg[%02X] <- %04X (not written)\n", bus, dev, fn, reg, v); return; }
    io_outd(0xCF8, cfg_addr(bus, dev, fn, reg)); io_outw(0xCFC + (reg & 2), v);
}
void cfg_write8(u8 bus, u8 dev, u8 fn, u8 reg, u8 v)
{
    if (g_dry) { out("DRY   %02X:%02X.%u cfg[%02X] <- %02X (not written)\n", bus, dev, fn, reg, v); return; }
    io_outd(0xCF8, cfg_addr(bus, dev, fn, reg)); io_outb(0xCFC + (reg & 3), v);
}

int dev_present(u8 bus, u8 dev, u8 fn)
{
    u32 id = cfg_read32(bus, dev, fn, 0);
    return id != 0xFFFFFFFFUL && id != 0;          /* 0 = no config mechanism (emulators) */
}
int pci_available(void)
{
    if (dev_present(0, 0, 0)) return 1;
    out("no PCI configuration mechanism #1\n");
    return 0;
}
u8 find_cap(u8 bus, u8 dev, u8 fn, u8 id)
{
    u8 ptr; int guard = 48;
    if (!(cfg_read16(bus, dev, fn, 6) & 0x10)) return 0;
    ptr = cfg_read8(bus, dev, fn, 0x34) & 0xFC;
    while (ptr && guard--) {
        if (cfg_read8(bus, dev, fn, ptr) == id) return ptr;
        ptr = cfg_read8(bus, dev, fn, ptr + 1) & 0xFC;
    }
    return 0;
}
u32 dev_class(u8 bus, u8 dev, u8 fn) { return cfg_read32(bus, dev, fn, 8) >> 8; }

const char *class_name(u32 cls)
{
    u16 c = (u16)(cls >> 8);
    switch (c) {
    case 0x0100: return "SCSI";        case 0x0101: return "IDE";         case 0x0104: return "RAID";
    case 0x0106: return "AHCI/SATA";   case 0x0107: return "SAS";         case 0x0180: return "storage";
    case 0x0200: return "ethernet";    case 0x0280: return "network";     case 0x0300: return "VGA";
    case 0x0380: return "display";     case 0x0400: return "video";       case 0x0401: return "audio";
    case 0x0403: return "HD audio";    case 0x0500: return "memory";      case 0x0600: return "host bridge";
    case 0x0601: return "LPC/ISA";     case 0x0604: return "PCI bridge";  case 0x0680: return "bridge";
    case 0x0700: return "serial";      case 0x0701: return "parallel";    case 0x0780: return "comm/MEI";
    case 0x0800: return "PIC";         case 0x0801: return "DMA";         case 0x0802: return "timer";
    case 0x0880: return "sys periph";  case 0x0900: return "input";       case 0x0C00: return "FireWire";
    case 0x0C03: return (cls & 0xFF) == 0x20 ? "USB2 EHCI" : (cls & 0xFF) == 0x30 ? "USB3 xHCI" :
                        (cls & 0xFF) == 0x10 ? "USB OHCI" : "USB UHCI";
    case 0x0C05: return "SMBus";       case 0x1000: return "crypto";      case 0x1180: return "thermal/DSP";
    default:     return "";
    }
}
/* Device classes for which no Win9x driver exists on any board: they sit with INTx enabled
 * on a shared level line, nobody ever acknowledges them -> the "handover" freeze. */
int class_driverless(u32 cls, u16 vid)
{
    u16 c = (u16)(cls >> 8);
    if (c == 0x0780) return 1;                       /* Intel MEI/HECI (management engine) */
    if (c == 0x0C05) return 1;                       /* SMBus controller */
    if (c == 0x0700 && vid == 0x8086) return 1;      /* Intel AMT KT serial redirection */
    if (c == 0x1180) return 1;                       /* thermal / signal processing */
    return 0;
}

void foreach_function(dev_cb cb, void *ctx)
{
    unsigned bus, last = pci_last_bus(); u8 dev, fn, maxfn;
    for (bus = 0; bus <= last; bus++)
        for (dev = 0; dev < 32; dev++) {
            if (!dev_present((u8)bus, dev, 0)) continue;
            maxfn = (cfg_read8((u8)bus, dev, 0, 0x0E) & 0x80) ? 8 : 1;
            for (fn = 0; fn < maxfn; fn++)
                if (dev_present((u8)bus, dev, fn)) cb((u8)bus, dev, fn, ctx);
        }
}
struct int_filter { dev_cb cb; void *ctx; };
static void int_filter_cb(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct int_filter *f = ctx;
    u8 pin = cfg_read8(bus, dev, fn, 0x3D);
    if (pin >= 1 && pin <= 4) f->cb(bus, dev, fn, f->ctx);
}
void foreach_int_device(dev_cb cb, void *ctx)
{
    struct int_filter f; f.cb = cb; f.ctx = ctx;
    foreach_function(int_filter_cb, &f);
}

int sel_parse(const char *s, struct sel *sel)
{
    unsigned a, b, c; unsigned long cls;
    memset(sel, 0, sizeof *sel);
    strncpy(sel->text, s, sizeof sel->text - 1);
    if (!strncmp(s, "class:", 6)) {
        size_t n = strlen(s + 6);
        if (sscanf(s + 6, "%lx", &cls) != 1) return 0;       /* long: 6 hex digits overflow a 16-bit int */
        if (n == 4)      { sel->cls = (u32)cls << 8; sel->clsmask = 0xFFFF00UL; }
        else if (n == 6) { sel->cls = (u32)cls;      sel->clsmask = 0xFFFFFFUL; }
        else return 0;
        sel->kind = SEL_CLASS; return 1;
    }
    if (strchr(s, '.')) {
        if (sscanf(s, "%x:%x.%u", &a, &b, &c) != 3 || a > 255 || b > 31 || c > 7) return 0;
        sel->kind = SEL_BDF; sel->bus = (u8)a; sel->dev = (u8)b; sel->fn = (u8)c; return 1;
    }
    if (strlen(s) == 9 && s[4] == ':' && sscanf(s, "%x:%x", &a, &b) == 2) {
        sel->kind = SEL_VD; sel->vid = (u16)a; sel->did = (u16)b; return 1;
    }
    return 0;
}
int sel_match(const struct sel *sel, u8 bus, u8 dev, u8 fn)
{
    u32 id;
    switch (sel->kind) {
    case SEL_BDF:   return bus == sel->bus && dev == sel->dev && fn == sel->fn;
    case SEL_VD:    id = cfg_read32(bus, dev, fn, 0);
                    return (u16)id == sel->vid && (u16)(id >> 16) == sel->did;
    case SEL_CLASS: return (dev_class(bus, dev, fn) & sel->clsmask) == sel->cls;
    }
    return 0;
}

/* ---- intx SEL [on|off]: PCI command register bit 10 = INTx disable ---------------------- */
struct intx_ctx { struct sel sel; int mode, n; };   /* mode -1 show, 0 enable, 1 disable */

static void intx_one(u8 bus, u8 dev, u8 fn, void *ctx)
{
    struct intx_ctx *c = ctx;
    u32 id; u16 cmd; u8 pin; int off;
    if (!sel_match(&c->sel, bus, dev, fn)) return;
    id = cfg_read32(bus, dev, fn, 0); pin = cfg_read8(bus, dev, fn, 0x3D);
    cmd = cfg_read16(bus, dev, fn, 4);
    c->n++;
    if (c->mode >= 0) {
        u16 nv = c->mode ? (u16)(cmd | 0x400) : (u16)(cmd & ~0x400);
        if (nv != cmd) cfg_write16(bus, dev, fn, 4, nv);
        cmd = cfg_read16(bus, dev, fn, 4);
    }
    off = (cmd & 0x400) != 0;
    out("INTX  %02X:%02X.%u %04X:%04X %-10s INTx %s (cmd=%04X)%s%s\n", bus, dev, fn,
        (unsigned)(id & 0xFFFF), (unsigned)(id >> 16), class_name(dev_class(bus, dev, fn)),
        off ? "off" : "ON", cmd, (pin < 1 || pin > 4) ? "  (no INT pin)" : "",
        (c->mode >= 0 && off != c->mode) ? (g_dry ? "  (dry-run)" : "  !! did not take") : "");
}
int cmd_intx(int argc, char **argv)
{
    struct intx_ctx c;
    memset(&c, 0, sizeof c); c.mode = -1;
    if (argc < 3 || !sel_parse(argv[2], &c.sel)) return 2;
    if (argc >= 4) {
        if (!strcmp(argv[3], "off")) c.mode = 1;
        else if (!strcmp(argv[3], "on")) c.mode = 0;
        else return 2;
    }
    if (!pci_available()) return 1;
    foreach_function(intx_one, &c);
    if (!c.n) { out("INTX  no device matches %s\n", c.sel.text); return 1; }
    return 0;
}
