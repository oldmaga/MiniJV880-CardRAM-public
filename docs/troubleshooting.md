# Troubleshooting

This page collects practical troubleshooting notes for MiniJV880-CardRAM.

It focuses on common setup and maintenance issues rather than low-level development diagnostics.

## The SD card boots but runtime material is missing

Check that the required runtime folders are present at the root of the FAT32 SD card.

Typical folders are:

    roms/
    CARD-RAM/
    PN-JV80/
    PN-JV80/Roland-PN/
    RD-500/

Only create optional folders if you use the related workflow.

The public release is source-only and does not include ROMs, SR data, PN data, RD-500 files, CardRAM images or SD-card images.

## Kernel file name

For Raspberry Pi 4, the active kernel image is expected on the SD-card root.

Depending on how the build output was produced, you may need to copy or rename the generated kernel as:

    kernel8-rpi4.img

## HTTP page errors or incomplete pages

The embedded HTTP server is intentionally small.

It is useful for control/status pages and small file-management pages, but it is not intended as a large generic file server.

Large payloads should use the documented TFTP workflows instead of HTTP download/upload paths.

## Large file transfer problems

Use Ethernet for normal maintenance.

Wi-Fi was investigated during development, but it is experimental and not supported for reliable HTTP/TFTP maintenance in this release line.

If a large TFTP transfer fails or times out:

- retry over Ethernet;
- verify the final file size on the SD card;
- avoid changing multiple variables at once;
- reboot only after confirming the intended file is present and complete.

## CardRAM selection does not change as expected

Check the `CARD-RAM/` folder and `current.txt`.

The CardRAM collection workflow expects `current.txt` to point to the active card image.

Card images should be valid 32768-byte CardRAM files.

## Card/C bank access is confusing

Use DATA long press in the supported contexts.

DATA short press remains reserved for the MiniJV880 SR Extension Card workflow.

MONITOR should not be used as the general Card/C selector in the current release line.

## PN-JV80 file operations are not available in a folder

The HTTP PN-JV80 workflow is intentionally shallow and restricted.

Keep user-managed `.syx` files in:

    PN-JV80/
    PN-JV80/<one-level-user-folder>/

The special folder:

    PN-JV80/Roland-PN/

is intended to be protected/read-only through the HTTP workflow.

## Public repo contains no runtime payloads

This is intentional.

The public repository should remain source-only and must not include:

    *.bin
    *.syx
    *.img
    *.patchslot
    CARD-RAM/
    PN-JV80/
    roms/
    RD-500/
    wpa_supplicant*
    tools/reports/

Use local/private storage for runtime material and experimental analysis outputs.
