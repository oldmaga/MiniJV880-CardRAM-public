# RD-500 optional files

MiniJV880-CardRAM can use an optional `RD-500/` folder on the SD card.

This is an optional convenience workflow. It is not required for the normal JV-880 emulation path and it is not part of a clean minimal setup.

## Runtime folder

The expected folder name is:

    RD-500/

Keep the name exactly as documented.

## Source-only release policy

The public MiniJV880-CardRAM release is source-only.

It does not include RD-500 data, ROMs, SysEx libraries, CardRAM images, SD-card images or any other copyrighted payloads.

Users who need optional RD-500 material must provide their own legally obtained files.

## When to create this folder

Create `RD-500/` only if you use the optional RD-500 related workflow exposed by the firmware build.

For a minimal JV-880/CardRAM setup, this folder can be omitted.

## Suggested layout

A clean SD-card layout may include:

    RD-500/

Do not place this folder inside `roms/`, `CARD-RAM/` or `PN-JV80/`.

## Notes

This folder is documented separately because it is optional and should not be confused with the required JV-880 runtime folders.

The core CardRAM and PN-JV80 workflows do not depend on RD-500 files.
