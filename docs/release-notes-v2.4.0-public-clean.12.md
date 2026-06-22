# MiniJV880-CardRAM v2.4.0-public-clean.12

This release adds physical serial MIDI OUT to the public Dualboot line while
preserving the existing MIDI IN, MIDI CC control and GPIO4 debug paths.

## Highlights

- Implements the emulated JV-880 UART transmit status, timing and interrupt
  behavior required by the original firmware.
- Forwards every completed emulated UART transmit byte to the physical Raspberry
  Pi MIDI UART.
- Uses a cross-core 16,384-byte MIDI output buffer and drains it according to
  the exact free space in the physical UART transmit ring.
- Preserves serial MIDI IN processing, including MIDI CC button control.
- Keeps the debug log on GPIO4 and prevents debug text from entering the MIDI
  UART.
- Does not change the optional two-system Dualboot layout: MiniJV880 remains
  the first system, while the second system may be MiniDexed or DreamDexed
  when the matching kernel and `.ini` file are installed.
- The public release remains Dualboot-only; Multiboot support for more than
  two systems is not included.

## Tested workflow

The public Raspberry Pi 4 Dualboot build was tested with a Roland UM-ONE mk2
connected to MiniJV880 MIDI OUT.

The following transfers completed successfully:

- `Utility -> Temporary Dump -> All`
  - LCD result: `COMPLETE`
  - 8,124 bytes received
  - 105 complete Roland DT1 packets
  - all 105 Roland checksums valid
  - no bytes outside SysEx and no truncated packet
- `Utility -> Bulk Dump -> INT > MIDI`
  - LCD result: `COMPLETE`
  - 45,795 bytes received
  - 525 complete Roland DT1 packets
  - all 525 Roland checksums valid
  - expected address range from `01 00 10 00` to `01 7F 7C 00`
  - no bytes outside SysEx and no truncated packet

Regression checks also confirmed:

- MIDI IN CC 62 still opens and closes the SR overlay.
- The GPIO4 diagnostic serial output remains active and readable.

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

Do not overwrite or modify the kernel or `.ini` file of the selected second
system, whether MiniDexed or DreamDexed. Keep the existing SD-card
configuration, ROMs, CardRAM images and personal settings unless another
documented change specifically requires updating them.

## Package notes

The release package is kernel-only. It is not a complete SD-card image and does
not include Raspberry Pi boot files, Roland ROM/NVRAM data, SR-JV80 images,
RD-500 data, SysEx banks, CardRAM images, Wi-Fi credentials or personal
configuration files.

The package checksum is provided with the release package.

## Source notes

This release is based on `v2.4.0-public-clean.11`.

The core MIDI OUT implementation is contained in commit:

    97640b7 Implement emulated UART MIDI OUT

The firmware change is limited to:

- `src/emulator/mcu.cpp`
- `src/emulator/mcu.h`
- `src/minijv880.cpp`
- `src/minijv880.h`

No MIDI CC mapping, default INI or PC-side MIDI button tool change is required
for MIDI OUT.
