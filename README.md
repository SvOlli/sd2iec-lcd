 SD2IEC-LCD
============

This is a fork of the official [SD2IEC](https://sd2iec.de/) firmware, with
an additional branch for support of LCDs based upon the version introduced
in the
[Forum64](https://www.forum64.de/index.php?thread/74116-lcd-sd2iec-firmware-1-0-0-f%C3%BCr-larsp-layout/).
It also includes changes done by Javi M to support the C64 Version of
"Another World" and "Wings Of Fury". Original source of this is available
at [github](https://github.com/jamarju/sd2iec-anotherworld/).

Branches
--------
These branches are available in this repository:

- master: contains the original code from [sd2iec.de](https://sd2iec.de/)
- LCD: contains the merged code for LCD versions
- c128d-internal: (this branch) contains additional code for a custom C128D
  internal mounted version

C128D Interal Version Additions
-------------------------------
A new command is implemented. "H" (hide): take the SD2IEC off the IEC bus,
like long pressing the next button. This is aliased to an "M-E"-command
executed by the
[C128 Device Manager](https://www.bartsplace.net/content/publications/devicemanager128.shtml)
, when it detects an SD2IEC using the ROM of the 1581.

A new compile option `CONFIG_USE_PREV_FOR_RESET` turns on following feature:
detecting IEC reset does the "unhide" from long pressing next button or the
"H"-command above, and also re-reads the config mainly to reset the drive id,
so a reset works similar to the reset of the internal 1571 drive. For this
to work, the IEC reset needs to be wired to the "PREV" button, which then
does no longer serve the original functionality.

See Also
--------
- [README](README) of the original SD2IEC
- [README_LCD](README_LCD) for the LCD changes
- [NEWS](NEWS) referring to the original SD2IEC
- [COPYING](COPYING) GPLv2 license

Note
----
The maintainer of this repository has done this effort, because he's got
an "evo2"-variant *without* and LCD. So there is no testing possible.

Pull requests are *very* welcome and will be processed in good faith.

Special thanks to Ingo Korb and CapFuture1975 and all the others
contributing to SD2IEC. This is be no means my work. I just had the luck,
that trying to pull some pieces together worked out pretty well.

-- SvOlli
