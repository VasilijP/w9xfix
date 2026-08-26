@ECHO OFF
REM IRQFIX.BAT - Dell Optiplex 990 (Q67) running Windows 98 SE in legacy PIC mode.
REM Called from AUTOEXEC.BAT for the CONFIG.SYS menu entry W9X, or by hand: IRQFIX W9X.
REM Applies with W9XFIX.EXE everything the BIOS leaves in a state Win98 cannot survive:
REM   * INTx off on the driverless devices that share level-triggered IRQs: MEI (class 0780),
REM     AMT KT serial (8086:1C3D), SMBus (class 0C05), the on-board NIC 00:19.0 when enabled
REM   * PCIe ASPM off on both ends of every link and in every function (GPU 01:00.0 and 01:00.1)
REM   * W9XFIX check verifies the resulting state; a failure is shown at boot for 10 s
REM   * EHCI BIOS-to-OS hand-off as the LAST step, if the marker C:\EHCIH.ON exists. The BIOS
REM     USB keyboard emulation stops there, so it is skipped when run by hand (the ESC entry's
REM     AUTOEXEC does it after EXIT).
REM BIOS pin routing is kept. Log: C:\DIAG\W9XFIX.TXT (every action with its read-back).
IF NOT "%1"=="" SET CONFIG=%1
IF NOT "%CONFIG%"=="W9X" GOTO END
IF NOT EXIST C:\DIAG\NUL MD C:\DIAG
ECHO IRQFIX: w9xfix - INTx off MEI/KT/SMBus/NIC, ASPM off on every link, EHCI hand-off before WIN
W9XFIX intx class:0780 off > C:\DIAG\W9XFIX.TXT
W9XFIX intx 8086:1C3D off >> C:\DIAG\W9XFIX.TXT
W9XFIX intx class:0C05 off >> C:\DIAG\W9XFIX.TXT
W9XFIX intx 00:19.0 off >> C:\DIAG\W9XFIX.TXT
W9XFIX aspm links 0 >> C:\DIAG\W9XFIX.TXT
W9XFIX show >> C:\DIAG\W9XFIX.TXT
W9XFIX check 00:19.0 >> C:\DIAG\W9XFIX.TXT
IF ERRORLEVEL 1 GOTO BAD
GOTO XCHK
:BAD
ECHO IRQFIX: *** W9XFIX check FAILED - see C:\DIAG\W9XFIX.TXT (continuing in 10 s) ***
CHOICE /C:YN /T:Y,10 /N > NUL
:XCHK
IF NOT "%1"=="" GOTO END
ECHO IRQFIX: applied - EHCI hand-off and Windows in 3 s
CHOICE /C:YN /T:Y,3 /N > NUL
IF EXIST C:\EHCIH.ON W9XFIX ehci handoff >> C:\DIAG\W9XFIX.TXT
:END
