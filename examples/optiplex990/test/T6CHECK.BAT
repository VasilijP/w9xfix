@ECHO OFF
ECHO T6CHECK: check before, real apply of the fix set (no hand-off), check after
W9XFIX check 00:19.0 > OUT\T6BEFORE.TXT
W9XFIX -l OUT\T6APPLY.TXT intx class:0780 off > NUL
W9XFIX -l OUT\T6APPLY.TXT intx 00:16.3 off > NUL
W9XFIX -l OUT\T6APPLY.TXT intx class:0C05 off > NUL
W9XFIX -l OUT\T6APPLY.TXT intx 00:19.0 off > NUL
W9XFIX -l OUT\T6APPLY.TXT aspm links 0 > NUL
W9XFIX check 00:19.0 > OUT\T6AFTER.TXT
IF ERRORLEVEL 1 ECHO T6CHECK: check after apply FAILED (see OUT\T6AFTER.TXT) >> OUT\RUNALL.TXT
