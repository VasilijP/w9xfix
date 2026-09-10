# w9xfix

[https://youtu.be/VS1iouD_OJQ](https://youtu.be/VS1iouD_OJQ?t=39)

A small real-mode DOS tool (16-bit, 386+) that fixes the three things a modern BIOS leaves in a
state Windows 9x cannot survive. Run it from `AUTOEXEC.BAT` before `WIN` starts.

| what the BIOS leaves behind | how Win9x suffers | fix |
|---|---|---|
| Platform devices no Win9x driver ever binds (Intel MEI/HECI, SMBus, AMT KT serial, an unused NIC) share a level-triggered IRQ with a real device; nobody acknowledges them, the line never goes quiet | freeze the moment the kernel enables interrupts, freezes in 3D games that share the line | `intx SEL off` – PCI INTx disable (command bit 10) |
| PCIe ASPM (L0s/L1) enabled on links whose endpoint cannot tolerate the exit latency, sometimes on one end or one function only; Win9x has no PCIe-aware driver | hard lockups under GPU load – no keyboard, power button dead | `aspm links 0` – ASPM off on both ends of every link, every function |
| The BIOS SMM handler keeps ownership of the EHCI USB controllers; the Win9x USB stack never gets them, every transfer completes by timeout | mouse/keyboard update once per second, minutes-long boots | `ehci handoff` – BIOS→OS hand-off through USBLEGSUP/USBLEGCTLSTS |

Developed and validated on a Dell Optiplex 990 (i5-2500, Q67, BIOS A24) running Windows 98 SE with
NUSB 3.6 and a PCIe Radeon (X550 / FireGL V3100). The mechanisms are generic PCI/PCIe; see *Scope*.

## Usage

```
w9xfix [-v] [-q] [-n] [-l FILE] command        -v verbose  -q quiet  -n dry-run  -l append log

show                          platform, chipset, SMI_EN, PIC, PIRQ routers, $PIR, every PCI function
check [SEL ...]               verify the fixed state; one RESULT PASS/FAIL line, errorlevel 0/1
intx SEL [on|off]             show / set INTx disable on every device matching SEL
aspm                          every PCIe link, both ends: [ON] [MISMATCH] width, latency safety
aspm links [0-3]              set ASPM on both ends of every link (0 = off, device end first)
aspm SEL [0-3]                one device
routers A,B,...,H             Intel PCH PIRQ routers (0 = disabled) + ELCR level bits
line                          rewrite each device's interrupt-line register from $PIR + routers
ehci [handoff] [SEL] [eecp=XX]  EHCI ownership; handoff = take the controllers from the BIOS

SEL = BB:DD.F | VVVV:DDDD | class:CCCC | class:CCCCPP     (bus:device.function, vendor:device, class)
```
Exit codes: 0 ok, 1 not found / did not take / check failed, 2 usage. Everything is printed as
one fact per line with a fixed key (`DEV`, `LINK`, `EHCI`, `CHECK`, ...), so `>>` a log file and grep it.

## Example: Optiplex 990, in `AUTOEXEC.BAT` before Windows

```bat
W9XFIX intx class:0780 off  > C:\W9XFIX.TXT    MEI (Intel Management Engine interface)
W9XFIX intx 8086:1C3D off  >> C:\W9XFIX.TXT    AMT KT serial redirection
W9XFIX intx class:0C05 off >> C:\W9XFIX.TXT    SMBus
W9XFIX intx 00:19.0 off    >> C:\W9XFIX.TXT    on-board NIC, no Win9x driver (skip if you use it)
W9XFIX aspm links 0        >> C:\W9XFIX.TXT    ASPM off on every link and function
W9XFIX check 00:19.0       >> C:\W9XFIX.TXT    RESULT PASS expected; errorlevel 1 otherwise
W9XFIX ehci handoff        >> C:\W9XFIX.TXT    LAST: from here the BIOS USB keyboard is dead
```
The complete boot batch with the menu logic is in `examples/optiplex990/IRQFIX.BAT`; the
per-feature self-tests that run on the box itself are in `examples/optiplex990/test/`.

What the tool sees on that machine before the fix (abridged):
```
CHIP  LPC 00:1F.0 8086:1C4E 6-series PCH  RCBA FED1C000  PMBASE 0400
SMI   SMI_EN=0002203B [GBL EOS LEGACY_USB SLP APMC TCO LEGACY_USB2]  (BIOS USB emulation SMIs armed)
DEV   00:16.0 8086:1C3A 0780 comm/MEI    pin A line 11  $PIR A=11 [driverless class, INTx ON]
DEV   01:00.0 1002:5B64 0300 VGA         pin A line 11  $PIR A=11
LINK  00:01.0 -> 01:00.0 1002:5B64  ASPM port 3/3 dev 3/3 [ON]  x16 of x16 2.5GT/s [L0s latency unsafe] [L1 latency unsafe]
EHCI  00:1A.0 8086:1C2D  BAR0 80D40000 cmd=0006  LEGSUP@68=00010001 [BIOS-owned]  LEGCTL=00002017 [USB ERR PORTCHG HSE OSOWN]
```
The MEI shares IRQ 11 with the GPU and has no driver; the PEG link runs L0s/L1 although the GPU
accepts only 128 ns / 2 µs of exit latency; the EHCIs are BIOS-owned with USB SMIs armed. Those
three lines are the three freezes.

## Finding out what to fix on another board

1. `w9xfix show` – look for `[driverless class, INTx ON]`, `[ASSERTING]`, `$PIR ... != line`.
2. `w9xfix aspm` – look for `[ON]`, `[MISMATCH]`, `[L0s/L1 latency unsafe]`, `[TRAINING DEGRADED]`.
3. `w9xfix ehci` – `[BIOS-owned]` with SMI enables means the hand-off is needed.
4. Try everything with `-n` first: it prints the exact register writes it would do.
5. `w9xfix check` at the end of the batch tells you every boot whether the state is still right.

## Build

* DOS binary: [Open Watcom v2](https://github.com/open-watcom/open-watcom-v2) – `./build.sh dos`
  (set `$WATCOM`, or unpack the snapshot into `build/watcom`). Output `build/W9XFIX.EXE`, ~35 KB.
* Logic test on any host compiler against a fake chipset: `./build.sh host`.
* Graceful-failure smoke in DOSBox-X (no PCI there): `./build.sh test`.

## Scope and limits

* `intx`, `aspm`, `ehci`, `check` are plain PCI/PCIe and work on any chipset.
* PIRQ routers and `$PIR` link decoding (`routers`, `line`, part of `show`) are Intel ICH/PCH
  up to the 9-series; the tool refuses to write routers on anything else.
* The EHCI legacy-support registers are assumed at config offset 68h (Intel and most others),
  verified by capability ID and reserved bits; `eecp=XX` overrides.

## License

MIT – see `LICENSE`.
