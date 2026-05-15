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

## Supported hardware and memory requirements

This MiniJV880-CardRAM line is developed and tested mainly on Raspberry Pi 4 Model B.

Recommended target:

- Raspberry Pi 4 Model B.
- A Raspberry Pi 4 model with enough RAM headroom for the number of SR-JV80 expansion images you plan to keep available.

Not recommended for this line:

- Raspberry Pi Zero class boards.
- Raspberry Pi 1 / 2 class boards.
- Raspberry Pi 3 / 3B / 3B+ class boards.
- Low-RAM configurations.

Raspberry Pi 5 is not the validation target for this line. It may require separate testing and possible build/runtime adjustments.

Important SR-JV80 memory note:

This build scans the SD-card `roms/` folder at boot and loads valid SR-JV80 expansion images into RAM. Each valid SR-JV80 expansion image uses about 8 MB of RAM. Keeping many SR images available at the same time increases boot-time memory usage.

For reliable operation, use a Raspberry Pi 4 with comfortable RAM headroom and keep only the SR-JV80 images you actually need on the SD card.

## Build and clean SD-card preparation

This repository is source-only. It does not provide a ready-made SD-card image.

GitHub releases for this repository are source releases. They do not include:

- compiled MiniJV880 kernel images;
- ready-made SD-card images;
- Raspberry Pi boot firmware files;
- Roland JV-880 ROM/NVRAM files;
- SR-JV80 expansion images;
- PN-JV80 SysEx files;
- RD-500 files;
- CardRAM images containing sound data;
- personal Wi-Fi configuration files.

A clean runtime SD card must be assembled by the user from:

- files built from this repository;
- Raspberry Pi/Circle boot files;
- legally obtained ROM/SR/PN/RD-500/CardRAM material;
- optional local configuration files.

### Local build workflow

The normal local build workflow is:

    source /path/to/your/env-minijv880.sh
    bash build.sh

A local environment file example is provided as:

    env-minijv880.example.sh

Adapt it for your own toolchain path and Raspberry Pi target. Do not commit personal absolute paths.

After a successful Raspberry Pi 4 build, the generated kernel image is placed in:

    src/

Depending on the build script, target and versioning workflow, the generated file in src may have a board-specific or build-specific name.

For the SD card, the active MiniJV880 kernel must be copied to the SD-card root and renamed, if necessary, to:

    kernel8-rpi4.img

The build script may also copy a versioned image into:

    releases/

Those release images are local build artifacts and are not included in the public source release.

### SD card format

Use a FAT32-formatted SD card.

MiniJV880 runs bare-metal. There is no Linux filesystem layout and no separate Linux root partition.

All runtime files and folders described below are placed in the root of the FAT32 SD card.

### Important folder naming and depth rules

Some folder names are part of the MiniJV880 runtime layout and should not be renamed.

Keep these names exactly as documented:

    roms/
    CARD-RAM/
    PN-JV80/
    PN-JV80/Roland-PN/
    RD-500/

If these folders are renamed, the related MiniJV880 feature may not find them.

Also keep the folder hierarchy shallow. Some MiniJV880 file-management and browse workflows are intentionally not recursive.

For example:

    PN-JV80/10-USER/file.syx

is a supported layout, while an additional nested level such as:

    PN-JV80/10-USER/subfolder/file.syx

may exist on the SD card but will not be seen or managed by the MiniJV880 HTTP/file workflow.

### Recommended clean SD card root layout

    /
    |-- Raspberry Pi boot files
    |-- kernel8-rpi4.img
    |-- config.txt
    |-- minijv880.ini
    |
    |-- jv880_rom1.bin
    |-- jv880_rom2.bin
    |-- jv880_waverom1.bin
    |-- jv880_waverom2.bin
    |-- jv880_nvram.bin
    |
    |-- roms/
    |     |-- optional SR-JV80 expansion images
    |
    |-- CARD-RAM/
    |     |-- optional CardRAM .bin images
    |     |-- current.txt
    |
    |-- PN-JV80/
    |     |-- Roland-PN/
    |     |-- 10-USER/
    |     |-- 20-USER/
    |     |-- other optional user-managed folders
    |
    |-- RD-500/
    |     |-- rd500_expansion.bin
    |     |-- rd500_patches.bin
    |
    |-- tools/
    |     |-- optional PC-side helper scripts/manuals

The layout above is a target runtime layout for a clean user-prepared SD card. It is not the content of this GitHub repository or release.

### Required runtime files

The following files are required for normal JV-880 operation:

    kernel8-rpi4.img
    config.txt
    minijv880.ini
    jv880_rom1.bin
    jv880_rom2.bin
    jv880_waverom1.bin
    jv880_waverom2.bin
    jv880_nvram.bin

The ROM/NVRAM files are not included in this repository. Users must provide them legally and separately.

Use the default source configuration as a starting point:

    src/config.txt
    src/minijv880.ini

Copy them to the SD-card root as:

    config.txt
    minijv880.ini

Review and adapt minijv880.ini for your own hardware, display and local network setup.

### Raspberry Pi boot files

The SD-card root must also contain the Raspberry Pi boot support files required by the target board, such as start/fixup/dtb/boot files.

These files are not included in this repository. Prepare them according to the Raspberry Pi/Circle boot workflow used by your build.

The presence of Raspberry Pi boot files for other boards does not imply that this MiniJV880 release is validated on those boards. This line is developed and tested mainly on Raspberry Pi 4 Model B.

### SR-JV80 expansion images

Optional SR-JV80 expansion images are placed in:

    roms/

This build scans the SD-card roms folder at boot and loads valid SR-JV80 expansion images into RAM.

Each valid SR-JV80 expansion image uses about 8 MB of RAM. Keep only the SR-JV80 images you actually need on the SD card, especially on low-RAM systems.

The roms folder is treated as protected/read-only by the MiniJV880 HTTP file-management pages.

SR-JV80 ROM files are not included in this repository. Users must obtain and use them legally and separately.

### CardRAM images

The recommended CardRAM collection folder is:

    CARD-RAM/

CardRAM image files are 32768 bytes each.

The file:

    CARD-RAM/current.txt

selects the active CardRAM image when using the collection workflow.

If no CardRAM image is available, MiniJV880 can generate a new CardRAM file. It can then be initialized/formatted from the instrument workflow.

The legacy root-level file:

    jv880_cardram.bin

may also be used as a fallback CardRAM image by older/simple workflows, but the CARD-RAM collection workflow is recommended.

CardRAM images containing patch, performance or rhythm data are not included in this repository.

### PN-JV80 and SysEx files

The optional folder:

    PN-JV80/

is used for organized SysEx material and related local SD-card workflows.

Typical user-managed subfolders may be named, for example:

    PN-JV80/10-USER/
    PN-JV80/20-USER/

These user folders can contain legally obtained or user-created .SYX files.

A special optional subfolder is:

    PN-JV80/Roland-PN/

This folder is intended for legally obtained Roland PN-JV80 SysEx material.

The labels in square brackets sometimes present in Roland PN-JV80 filenames, such as [SR-xx] or [INTERNAL], are only reminders of the required sound source/context. For example, an [SR-xx] label means that the file is intended to be used with the corresponding SR-JV80 expansion available/loaded. [INTERNAL] refers to internal JV-880 data.

The Roland-PN folder is treated as protected/read-only by the MiniJV880 HTTP file-management pages, similarly to the roms folder. It is intended for reference/download access, not for HTTP-side rename, move, delete or upload operations.

Other PN-JV80 subfolders may be used for user-managed SysEx material when the related HTTP/TFTP features are enabled.

SysEx sound banks and PN-JV80 files are not included in this repository. Users must obtain and use them legally and separately.

In the default configuration, HTTP exposure of PN-JV80 and roms is disabled by default. Enable it only after reviewing your SD-card contents and local network setup.

### RD-500 optional files

MiniJV880 can also use optional RD-500-related files:

    RD-500/rd500_expansion.bin
    RD-500/rd500_patches.bin

These files must be placed in the SD-card folder:

    RD-500/

When present, they can be loaded from the MiniJV880 SR menu/overlay workflow.

RD-500 files are not included in this repository. Users must obtain and use them legally and separately.

### Network note: Ethernet and Wi-Fi

Ethernet is the recommended network path for MiniJV880 HTTP/TFTP maintenance in this release.

Wi-Fi/WLAN code and configuration experiments may be present in the source tree, but Wi-Fi is not considered reliable or supported in this v2.4.0 line.

For reliable file management, kernel staging, INI staging, CardRAM upload/download and PN-JV80 SysEx workflows, use wired Ethernet.

Do not commit personal Wi-Fi configuration files such as wpa_supplicant.conf or hotspot/router-specific variants. Create your own local Wi-Fi configuration only if you are experimenting with the unsupported WLAN path.

### PC-side tools

The tools in the repository `tools/` directory are PC-side helper utilities. They are not MiniJV880 firmware components and are not required by the MiniJV880 firmware at boot.

They can be used from the development PC to inspect/edit CardRAM images, launch the CardRAM GUI, transfer files to the MiniJV880, stage kernel/INI updates and calculate MiniJV880 file digests.

Main PC-side tools:

    tools/minijv880_cardram_tool.py
    tools/minijv880_cardram_gui.py
    tools/minijv880_tftp_gui.sh
    tools/minijv880_tftp_put.py

CardRAM GUI:

    python3 tools/minijv880_cardram_gui.py

TFTP helper GUI:

    tools/minijv880_tftp_gui.sh

TFTP helper GUI with explicit MiniJV880 IP address:

    MINIJV880_HOST=192.168.1.50 tools/minijv880_tftp_gui.sh

Documentation:

    tools/minijv880_cardram_tool_manual_en.txt
    tools/minijv880_cardram_tool_manual_it.txt
    tools/minijv880_tftp_helper_manual_en_v4.txt
    tools/minijv880_tftp_helper_manual_it_v4.txt

Platform notes:

- The CardRAM command-line tool and CardRAM GUI are Python 3 tools. The GUI uses Tkinter.
- The CardRAM tools are developed/tested mainly on Linux. They may work on other desktop platforms with Python 3 and Tkinter available, but Windows/macOS are not validation targets for this release.
- The TFTP helper GUI is Linux-oriented because it is a shell script workflow. On Windows, use WSL/Linux or adapt the workflow manually.
- The MiniJV880 network maintenance workflow is intended for local wired Ethernet use.

See the manuals above for detailed usage.

### Optional tools folder on the SD card

The optional SD-card folder:

    tools/

may contain a convenience copy of the PC-side helper scripts and manuals from this repository.

These files are not required by the MiniJV880 firmware at boot.


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
