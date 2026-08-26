#!/bin/bash
# build.sh - w9xfix builds
#   ./build.sh host     host-side logic build + smoke test (any cc; fake PCI space, src/io_host.c)
#   ./build.sh dos      W9XFIX.EXE via Open Watcom v2 (set $WATCOM, or unpack the ow-snapshot into
#                       build/watcom; macOS host binaries = bino64, Linux = binl64)
#   ./build.sh test     run the DOS binary in DOSBox-X (no PCI there - checks graceful failure)
#   ./build.sh release  promote build/W9XFIX.EXE to release/ (the binary deploy-ssd.sh puts on the boot path)
set -e
cd "$(dirname "$0")"
mkdir -p build
SRC="main.c pci.c pic.c intel_pch.c aspm.c ehci.c show.c"
fail() { echo "SMOKE FAIL: $*"; exit 1; }
case "${1:-host}" in
host)
    cc -DHOSTTEST -Wall -Wextra -std=c89 -Wno-long-long -o build/w9xfix_host $(for f in $SRC; do echo src/$f; done) src/io_host.c
    H=./build/w9xfix_host
    $H show
    $H -v show | grep -q 'subsys=17AF:3000' || fail "verbose show"
    $H show | grep -q '00:16.0 8086:1C3A 0780 comm/MEI .*\[ASSERTING\] \[driverless class, INTx ON\]' || fail "show flags"
    $H show | grep -q 'SMI   SMI_EN=0002203B \[GBL EOS LEGACY_USB SLP APMC TCO LEGACY_USB2\]' || fail "SMI_EN decode"
    $H intx class:0780 off | grep -q '00:16.0 8086:1C3A comm/MEI   INTx off' || fail "intx by class"
    $H -n intx 8086:1C3A off | grep -q 'DRY   00:16.0 cfg\[04\] <- 0400' || fail "dry-run intx"
    $H aspm | grep -q 'ASPM port 3/3 dev 1/3 \[MISMATCH\] \[ON\]  x4 of x16 2.5GT/s \[TRAINING DEGRADED\] \[L0s latency unsafe\]' || fail "aspm link report"
    $H aspm links 0 | grep -q '=> port 0 dev 0' || fail "aspm links 0"
    $H ehci | grep -q 'LEGSUP@68=00010001 \[BIOS-owned\]  LEGCTL=00002017 \[USB ERR PORTCHG HSE OSOWN\]' || fail "ehci show"
    $H ehci handoff 00:1A.0 | grep -q 'LEGSUP=01000001 \[OS-owned\]  LEGCTL=00000000 \[\]' || fail "ehci handoff"
    $H ehci handoff 00:1D.0 >/dev/null && fail "missing EHCI must fail"
    $H check 00:16.0 | grep -q 'RESULT FAIL' || fail "check must fail on the untouched fake"
    $H check 00:1D.0 | grep -q 'not present (disabled in BIOS?) -> WARN' || fail "check listed missing"
    $H intx class:0C0320 | grep -q '00:1A.0 8086:1C2D USB2 EHCI' || fail "6-digit class selector"
    $H -n ehci handoff | grep -q '(dry-run: not verified)' || fail "dry-run ehci"
    $H -n ehci handoff >/dev/null || fail "dry-run ehci rc"
    $H ehci eecp=50 | grep -q 'no USB legacy-support capability at 50' || fail "eecp reserved-bits check"
    rm -f build/smoke.log; $H -q -l build/smoke.log show >/dev/null; grep -q '^CHIP  LPC 00:1F.0 8086:1C4E 6-series PCH' build/smoke.log || fail "-l log"
    [ -z "$($H -q show)" ] || fail "-q must silence stdout"
    rc=0; $H badcmd >/dev/null || rc=$?; [ "$rc" = 2 ] || fail "usage rc ($rc)"
    echo "host build + smoke OK"
    ;;
dos)
    WATCOM=${WATCOM:-$PWD/build/watcom}
    [ -d "$WATCOM" ] || { echo "set \$WATCOM or unpack the toolchain:"; \
        echo "  curl -LO https://github.com/open-watcom/open-watcom-v2/releases/download/Current-build/ow-snapshot.tar.xz"; \
        echo "  mkdir -p build/watcom && tar -xJf ow-snapshot.tar.xz -C build/watcom bino64 binl64 h lib286"; exit 2; }
    case "$(uname -s)" in
        Darwin) HOSTBIN=$WATCOM/bino64;;
        Linux)  HOSTBIN=$WATCOM/binl64;;
        *)      HOSTBIN=$WATCOM/binnt64;;
    esac
    export WATCOM PATH="$HOSTBIN:$PATH" INCLUDE="$WATCOM/h"
    ( cd build && wcl -3 -ms -ox -bcl=dos -fe=W9XFIX.EXE $(for f in $SRC; do echo ../src/$f; done) ../src/io_dos.c > wcl.log 2>&1 ) || { grep -v -E '^(Open Watcom|Version|Copyright|Portions|Source code|See https)' build/wcl.log; exit 1; }
    grep -E 'warnings|Error|error' build/wcl.log | grep -v ', 0 warnings, 0 errors' || true
    ls -la build/W9XFIX.EXE
    ;;
test)
    command -v dosbox-x >/dev/null || { echo "brew install dosbox-x"; exit 2; }
    T=build/dbxtest; mkdir -p $T; cp build/W9XFIX.EXE $T/; rm -f $T/OUT.TXT
    cat > $T/dbx.conf <<EOF
[sdl]
output=surface
[autoexec]
mount c $PWD/$T
c:
W9XFIX show > OUT.TXT
W9XFIX check >> OUT.TXT
W9XFIX -n aspm links 0 >> OUT.TXT
W9XFIX -l OUT.TXT -q ehci
W9XFIX badcmd >> OUT.TXT
exit
EOF
    timeout 60 dosbox-x -nogui -conf $T/dbx.conf >/dev/null 2>&1 || true
    pkill -f "Quit DOSBox-X warning" 2>/dev/null || true
    cat $T/OUT.TXT
    ;;
release)
    mkdir -p release && cp build/W9XFIX.EXE release/W9XFIX.EXE && md5 -q release/W9XFIX.EXE 2>/dev/null || md5sum release/W9XFIX.EXE
    ;;
*) echo "usage: build.sh host|dos|test|release"; exit 2;;
esac
