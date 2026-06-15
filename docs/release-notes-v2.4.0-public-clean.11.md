# MiniJV880-CardRAM v2.4.0-public-clean.11

This release updates the public-clean MiniJV880/CardRAM source snapshot with the tested MIDI-only control workflow for local MiniJV880 overlay menus.

## Highlights

- Adds/finalizes MIDI CC control for the PN-JV80/SYX browser:
  - `MIDIButtonEnter=59` now works inside the PN-JV80/SYX browser to enter folders or load the selected `.SYX` file.
  - `MIDIButtonUp=60` and `MIDIButtonDown=61` provide DATA dial steps in normal screens and in local overlays.
  - `MIDIButtonSROverlay=62` toggles the SR overlay without using the physical DATA button.
  - `MIDIButtonEnterLong=63` opens the PN-JV80/SYX browser and works as back/exit while the browser is open.
  - `MIDIButtonAllRelease=64` remains available as a panic/safety command for clearing MIDI-held states.
- Updates the PC-side MIDI button test tool help/labels to document the tested SR/SYX MIDI workflow.
- Updates the public documentation and default INI comments to describe the MIDI CC workflow.

## Tested workflow

The following workflow was tested with a PC MIDI interface connected to MiniJV880 MIDI IN:

1. CC 62 opens/closes the SR overlay.
2. CC 63 opens the PN-JV80/SYX browser.
3. CC 60 / CC 61 navigate through the SYX browser.
4. CC 59 enters folders or loads the selected `.SYX` file.
5. CC 63 backs out of a subfolder or exits from the root browser level.

## Package notes

The release package is kernel-only. It is not a complete SD-card image and does not include Raspberry Pi boot files, Roland ROM/NVRAM data, SR-JV80 images, RD-500 data, SysEx banks, CardRAM images, Wi-Fi credentials or personal configuration files.

For dualboot setups, replace only the managed MiniJV880 kernel, typically:

    SD:/minijv880/kernel8-rpi4.img

Do not replace or delete the MiniDexed kernel.

## Source notes

The source tag for this release is expected to be:

    v2.4.0-public-clean.11

The previous public-clean release tag was:

    v2.4.0-public-clean.10
