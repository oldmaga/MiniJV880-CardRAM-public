# MiniJV880-CardRAM v2.4.0-public-clean.12

> **Corrected reissue — June 24, 2026**
>
> The original `v2.4.0-public-clean.12` source tag and kernel package did not
> contain the compact Remote HTTP backend required by the documented PC-side
> Remote GUI. This corrected reissue replaces the original `.12` tag and binary
> assets while preserving the MIDI OUT implementation introduced by `.12`.
>
> Users who downloaded the earlier `.12` kernel-only package should replace the
> MiniJV880 kernel with the corrected package. The optional Dualboot layout and
> the MiniDexed or DreamDexed second-system files must not be replaced.

## Highlights

- Implements physical serial MIDI OUT through the Raspberry Pi UART.
- Restores the compact Remote HTTP backend used by the external PC-side Remote
  GUI.
- Restores LCD and LED readback through:
  - `/rlcd.txt`
  - `/rled.txt`
- Restores Remote button and encoder endpoints:
  - `/rraw`
  - `/rdown`
  - `/rup`
  - `/renc`
  - `/rclr`
- Supports Remote tap, press-and-hold, release, encoder rotation and input-state
  clearing.
- Keeps Remote input state independent from physical GPIO buttons and MIDI CC
  buttons, then combines all three input paths safely.
- Preserves serial MIDI IN processing, MIDI CC button control and the separate
  GPIO4 debug-log output.
- Keeps the complete Remote control panel external to the MiniJV880. The
  embedded HTTP server continues to expose only compact technical endpoints.
- Does not change the optional two-system Dualboot layout: MiniJV880 remains the
  first system, while the second system may be MiniDexed or DreamDexed when the
  matching kernel and `.ini` file are installed.
- The public release remains Dualboot-only. Experimental Multiboot support is
  not included.

## Corrected build validation

The corrected Raspberry Pi 4 kernel was built from the public repository and
tested on the MiniJV880 hardware.

Validated kernel:

    size:   1,288,920 bytes
    SHA256: a652376e3110b389fdb70b5b17632da76f78ff75fcb539a10a2e583092e37051

The superseded kernel contained in the original `.12` package had SHA256:

    93e9453dcb76a8bdd65e1a7f4fc614e104a401ce340f9bc4104b8276210f3efe

The following regression checks passed with the corrected kernel:

- normal MiniJV880 boot;
- firmware version displayed as `2.4.0`, without a `-dirty` suffix;
- physical button input;
- MIDI CC button input;
- external Remote GUI connection;
- LCD readback;
- Remote tap and press-and-hold;
- Remote encoder rotation;
- Remote input-state clearing;
- physical MIDI OUT.

A `Utility -> Temporary Dump -> PATCH` transfer also completed successfully:

- exactly 553 bytes received;
- five complete Roland DT1 messages;
- expected Temporary Patch address sequence;
- all Roland checksums valid;
- no truncated message.

## MIDI OUT implementation

The emulated JV-880 UART transmit path implements the status, timing and
interrupt behavior required by the original firmware.

Every completed emulated UART transmit byte is forwarded to the physical
Raspberry Pi MIDI UART through a cross-core 16,384-byte output buffer. The
buffer is drained according to the available space in the physical UART
transmit ring.

The original `.12` MIDI OUT validation included:

- `Utility -> Temporary Dump -> All`
  - LCD result: `COMPLETE`
  - 8,124 bytes received
  - 105 complete Roland DT1 packets
  - all 105 Roland checksums valid
- `Utility -> Bulk Dump -> INT > MIDI`
  - LCD result: `COMPLETE`
  - 45,795 bytes received
  - 525 complete Roland DT1 packets
  - all 525 Roland checksums valid
  - expected address range from `01 00 10 00` to `01 7F 7C 00`

MIDI IN CC 62 continues to open and close the SR overlay, and the GPIO4
diagnostic serial output remains active and separate from the MIDI UART.

## MIDI hardware note

The standard Raspberry Pi UART is used for MIDI at 31,250 baud:

- GPIO14 / TXD: MIDI OUT
- GPIO15 / RXD: MIDI IN

GPIO4 remains the separate 38,400-baud debug-log TX output.

MIDI OUT requires a proper MIDI current-loop output circuit suitable for 3.3 V
logic. Do not connect GPIO14 directly to a 5-pin DIN socket. MIDI shields
designed around 5 V logic may require verified resistor or level adaptation
specific to their schematic.

## Upgrade notes

For the documented optional Dualboot layout, replace only:

    /minijv880/kernel8-rpi4.img

Do not overwrite or modify:

- the MiniDexed or DreamDexed kernel;
- the second system `.ini` file;
- `config.txt`;
- BootSelector;
- ROMs, CardRAM images or personal settings.

For a single-system installation, replace the MiniJV880 kernel at the location
used by the existing SD-card configuration.

## Package notes

The release package is kernel-only. It is not a complete SD-card image and does
not include Raspberry Pi boot files, Roland ROM/NVRAM data, SR-JV80 images,
RD-500 data, SysEx banks, CardRAM images, Wi-Fi credentials or personal
configuration files.

The package contains:

- `kernel8-rpi4.img`
- `README-SD-ROOT.txt`
- `SHA256SUMS.txt`

A separate checksum and manifest are supplied with the GitHub release assets.

## Source notes

This release remains based on `v2.4.0-public-clean.11`.

The core MIDI OUT implementation is contained in:

    97640b7 Implement emulated UART MIDI OUT

The corrected compact Remote backend is contained in:

    6732127 Restore compact remote-control backend

The Remote source restoration modifies only:

- `src/minijv880.cpp`
- `src/netfileserver.cpp`

The correction preserves the previously published MIDI OUT source changes and
does not add private CardRAM investigations, SysEx experiments or experimental
Multiboot code.
