/* w9xfix - Win9x-on-modern-hardware fixer (github.com/VasilijP/w9xfix, MIT)
 *
 * One real-mode DOS tool for the three things a modern BIOS leaves in a state Win9x cannot
 * survive: driverless platform devices storming shared level IRQs (Intel MEI above all),
 * PCIe ASPM left on (hangs early PCIe GPUs), and EHCI controllers the BIOS SMM handler never
 * releases (USB on timeouts). Run from AUTOEXEC.BAT before WIN; 386+ required.
 * Exit codes: 0 ok, 1 not found / did not take / check failed, 2 usage.
 */
#include "w9xfix.h"

int g_verbose, g_quiet, g_dry;
static FILE *g_logf;

void out(const char *fmt, ...)
{
    va_list ap;
    if (!g_quiet) { va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
    if (g_logf)   { va_start(ap, fmt); vfprintf(g_logf, fmt, ap); va_end(ap); }
}

static const char usage[] =
    "w9xfix " W9XFIX_VERSION " - Win9x on modern hardware, runtime fixes (MIT)\n"
    "  w9xfix [-v] [-q] [-n] [-l FILE] command ...     -v verbose  -q quiet  -n dry-run  -l log\n"
    "  show                         platform, chipset, SMI_EN, PIC, routers, $PIR, every device\n"
    "  check [SEL ...]              verify the fixed state (driverless INTx off, links off,\n"
    "                               EHCIs OS-owned, nothing asserting); SEL = extra INTx-off\n"
    "  intx SEL [on|off]            show/set INTx disable (cmd bit 10) on matching devices\n"
    "  aspm                         every PCIe link, both ends: [ON] [MISMATCH] width, latency\n"
    "  aspm links [0-3]             set every link's both ends (0 = off, device end first)\n"
    "  aspm SEL [0-3]               show/set one device's ASPM field\n"
    "  routers A,B,..,H             Intel PCH PIRQ routers (0 = disabled) + ELCR level bits\n"
    "  line                         rewrite cfg 3Ch from $PIR links + routers\n"
    "  ehci [handoff] [SEL] [eecp=XX]  EHCI BIOS/OS ownership; handoff = take them (kills\n"
    "                               BIOS USB keyboard - last thing before WIN)\n"
    "  SEL = BB:DD.F | VVVV:DDDD | class:CCCC | class:CCCCPP\n";

int main(int argc, char **argv)
{
    int i = 1, k, rc = 2;
    char tbuf[16];
    const char *logpath = NULL;
#ifdef HOSTTEST
    host_init();
#endif
    while (i < argc && argv[i][0] == '-') {
        if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else if (!strcmp(argv[i], "-q")) g_quiet = 1;
        else if (!strcmp(argv[i], "-n")) g_dry = 1;
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) logpath = argv[++i];
        else { fputs(usage, stdout); return 2; }
        i++;
    }
    argc -= i - 1; argv += i - 1;                 /* argv[1] = command */
    if (logpath && !(g_logf = fopen(logpath, "a"))) { printf("cannot open log %s\n", logpath); return 2; }
    if (argc < 2) { out("%s", usage); goto done; }
    sys_time_str(tbuf);
    out("=== w9xfix " W9XFIX_VERSION);
    for (k = 1; k < argc; k++) out(" %s", argv[k]);
    out("  (%s%s)\n", tbuf, g_dry ? ", DRY-RUN: nothing is written" : "");

    if (!strcmp(argv[1], "show"))                       rc = cmd_show();
    else if (!strcmp(argv[1], "check"))                 rc = cmd_check(argc, argv);
    else if (!strcmp(argv[1], "intx"))                  rc = cmd_intx(argc, argv);
    else if (!strcmp(argv[1], "aspm"))                  rc = cmd_aspm(argc, argv);
    else if (!strcmp(argv[1], "routers") && argc >= 3)  rc = cmd_routers(argv[2]);
    else if (!strcmp(argv[1], "line"))                  rc = cmd_line();
    else if (!strcmp(argv[1], "ehci"))                  rc = cmd_ehci(argc, argv);
    if (rc == 2) out("%s", usage);
    else if (rc || g_verbose) out("=== rc=%d\n", rc);
done:
    if (g_logf) fclose(g_logf);
    return rc;
}
