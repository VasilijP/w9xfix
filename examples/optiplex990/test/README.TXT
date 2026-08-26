W9XTEST - w9xfix self-tests on real hardware (reference: Dell Optiplex 990)
=====================================================================
Run from the ESC menu entry (nothing applied, BIOS state) or from any DOS prompt:
    C:
    CD \W9XTEST
    RUNALL
Every T*.BAT is independent and writes its own OUT\T*.TXT (one failure does not stop the
rest). Copy W9XFIX.EXE into this directory first; keep the copy used at boot separate.

T1SHOW   show, -v show                          read-only
T2ASPM   link report, -v, dry-run links 0,      read-only (dry-run writes nothing)
         single-device forms
T3EHCI   ehci, -v, dry-run handoff              read-only
T4INTX   intx by BDF / vendor:device / class,   read-only except the last step, which
         dry-run, then REAL intx off on the MEI  really silences 00:16.0 (same as at boot)
T5ROUTE  routers / line dry-run                 read-only (dry-run)
T6CHECK  check before, REAL apply of the whole  writes: intx off x4, aspm links 0 -
         fix set, check after                    identical to what W9X does at boot;
                                                 NO EHCI hand-off (keyboard stays)
T7LOG    -l logging, -q, usage/rc, bad input    read-only

After RUNALL, EXIT continues into Windows (AUTOEXEC hands the EHCIs off first if
C:\EHCIH.ON exists) - the fixes applied by T4/T6 are the normal ones.
