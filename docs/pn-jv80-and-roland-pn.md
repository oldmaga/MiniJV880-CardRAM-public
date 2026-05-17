# PN-JV80 and Roland-PN workflow

MiniJV880-CardRAM includes a local SD-card workflow for JV-880 compatible PN/JV80 SysEx material.

This feature is intended for managing user-provided files on the SD card. The project does not include commercial ROMs, PN data, SR data, SysEx collections or other copyrighted payloads.

## Runtime folders

The relevant SD-card folders are:

    PN-JV80/
    PN-JV80/Roland-PN/

Folder names are significant and should be kept exactly as documented.

## PN-JV80 user area

The `PN-JV80/` folder is the general local user area for PN/JV80 SysEx files.

The embedded HTTP browser supports browsing this area and, where enabled by the current firmware build, basic file-management operations for `.syx` files.

Typical operations are:

- browse folders;
- download `.syx` files;
- copy `.syx` files;
- rename `.syx` files;
- move `.syx` files;
- delete `.syx` files.

The firmware intentionally keeps this workflow simple and local. It is not a general-purpose file manager.

## Folder-depth rule

The PN-JV80 browser is intentionally shallow.

The supported model is:

    PN-JV80/
    PN-JV80/<one-level-user-folder>/
    PN-JV80/Roland-PN/

Recursive deep browsing is not a design goal. Keep user files in the root of `PN-JV80/` or in one-level subfolders.

## Roland-PN protected area

`PN-JV80/Roland-PN/` is treated as a special protected area by the HTTP workflow.

In the current MiniJV880-CardRAM design, this folder is meant to be read-only through the embedded HTTP interface.

The intent is to reduce the risk of accidental changes to a reference PN collection.

If a user wants to reorganize or rename those files, they should do it manually on the SD card or by copying material into another user folder under `PN-JV80/`.

## Recommended usage

A practical layout is:

    PN-JV80/
    PN-JV80/10-USER/
    PN-JV80/20-ARCHIVE/
    PN-JV80/Roland-PN/

Use ordinary user folders for files that may be renamed, moved, copied or deleted through the network interface.

Keep `Roland-PN/` for reference material that should not be modified remotely.

## Network notes

Ethernet is the recommended network path for this workflow.

Wi-Fi experiments were performed during development, but Wi-Fi is not considered supported for normal file-management use in this release line.
