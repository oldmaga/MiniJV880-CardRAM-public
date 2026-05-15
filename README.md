# MiniJV880-CardRAM

MiniJV880-CardRAM is a source-only public snapshot of my MiniJV880 work, based on the private development state tagged as v2.4.0.

This version focuses on practical standalone use of MiniJV880 on Raspberry Pi 4, with CardRAM management, local network maintenance support and improved front-panel control handling.

This repository does not include Roland ROMs, SR-JV80 ROMs, SysEx sound banks, CardRAM images containing sound data, compiled firmware images, SD-card images or external test dumps.

Users must provide any required ROM or sound data legally and separately.

## Status

Public-clean snapshot:

- Version: 2.4.0
- Base: Mini-JV880 / Mini-JV880pi family
- Target platform: Raspberry Pi 4
- Distribution type: source only

This public repository is intentionally cleaner than the private development repository. Experimental SysEx/CardRAM mapping research, private reports and non-public test material are not included here.

## Main features in this v2.4.0 line

### DATA short press and long press

This build uses the DATA button with two different roles:

- DATA short press: SR overlay access.
- DATA long press: native A/B/I/C / Card bank selection pass-through.

The DATA long press Card/C behavior was tested in:

- Patch Play
- Performance Play
- Patch Write
- Performance Write
- Patch Copy
- Performance Copy

The MONITOR button is no longer used as a Card/C fallback. It remains preserved for its own Performance Play function.

### CardRAM support

This line includes MiniJV880 CardRAM work, including:

- CardRAM collection management.
- CardRAM selection support.
- CardRAM probe wrap fix during Patch Play navigation.
- PC-side CardRAM inspection/editing tools.
- Tkinter CardRAM manager GUI.
- Support for exporting and importing raw .patchslot files.

The repository includes CardRAM tools, but it does not include CardRAM images containing patch, performance or rhythm sound data.

If no CardRAM image is available on the SD card, MiniJV880 can generate a new CardRAM file, which can then be initialized/formatted from the instrument workflow.

### Network maintenance

This line contains HTTP/TFTP maintenance work for local MiniJV880 management.

The public default src/minijv880.ini is intentionally conservative:

    NetDHCP=1
    NetWriteEnable=0
    NetExposePNJV80=0
    NetExposeRoms=0
    NetTFTPEnable=0

Enable network write, TFTP and folder exposure only after reviewing the configuration and the files present on your SD card.

## What is not included

This repository intentionally does not include:

- Roland JV-880 ROMs.
- SR-JV80 ROMs.
- SysEx sound banks.
- CardRAM images containing sound data.
- Commercial patches.
- Compiled firmware images.
- SD-card images.
- External test dumps.

The .gitignore file is configured to avoid accidentally adding common local/private data such as .bin, .syx, .patchslot, CARD-RAM/, PN-JV80/ and roms/.

## Required external files

MiniJV880 requires ROM files supplied by the user.

Typical SD-card files expected by the MiniJV880 family include files such as:

    jv880_rom1.bin
    jv880_rom2.bin
    jv880_waverom1.bin
    jv880_waverom2.bin
    jv880_nvram.bin

Optional SR-JV80 expansion images are normally stored in the SD-card roms/ folder.

These files are not provided by this repository.

## CardRAM tools

The PC-side CardRAM tool and GUI are in:

    tools/minijv880_cardram_tool.py
    tools/minijv880_cardram_gui.py

Documentation:

    tools/minijv880_cardram_tool_manual_en.txt
    tools/minijv880_cardram_tool_manual_it.txt

Basic examples:

    python3 tools/minijv880_cardram_tool.py check-card your_card.bin
    python3 tools/minijv880_cardram_tool.py summary your_card.bin
    python3 tools/minijv880_cardram_gui.py

Use only CardRAM files that you are legally allowed to use.

## TFTP helper

The PC-side TFTP helper files are:

    tools/minijv880_tftp_gui.sh
    tools/minijv880_tftp_put.py

Documentation:

    tools/minijv880_tftp_helper_manual_en_v4.txt
    tools/minijv880_tftp_helper_manual_it_v4.txt

The public default configuration disables TFTP. Enable it explicitly only when needed for local maintenance.

## Hardware box

The hardware/STL box files are maintained separately:

    https://github.com/oldmaga/MiniJV-880-Box

## Licenses and attribution

This project is derived from several upstream projects and has a mixed license structure.

See:

    LICENSE
    ATTRIBUTIONS.md

Important summary:

- Nuked-SC55-derived emulator code remains under the original MAME license terms used by Nuked-SC55, including its non-commercial restriction.
- GPL-3.0-covered code remains under GPL-3.0.
- Third-party submodules remain governed by their own upstream licenses.
- This repository does not include Roland ROMs or sound content.

## Legacy documentation

Older SR robust build notes are preserved here:

    docs/legacy-sr-robust-build-readme.txt

Those notes are useful historical/operational context, but this README is the main public entry point for the v2.4.0 CardRAM-focused public snapshot.
