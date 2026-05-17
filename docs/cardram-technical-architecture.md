# CardRAM technical architecture

This page summarizes the main implementation points of CardRAM support in MiniJV880-CardRAM.

## Overview

CardRAM support is split into four main layers:

    emulator / MCU layer
    MiniJV880 runtime layer
    HTTP/TFTP network management layer
    PC-side tools

The core idea is simple: the JV-880 firmware sees a 32 KB emulated Data Card RAM area, while MiniJV880 backs that memory with a `.bin` file on the SD card.

## Emulator layer

Main files:

    src/emulator/mcu.h
    src/emulator/mcu.cpp

`src/emulator/mcu.h` defines the CardRAM storage:

    CARDRAM_SIZE = 0x8000
    cardram[CARDRAM_SIZE]
    cardram_dirty
    cardram_seq

`src/emulator/mcu.cpp` maps firmware reads/writes to the emulated `cardram[]` buffer.

When the firmware writes to the card area, the emulator stores the byte, marks `cardram_dirty` and increments `cardram_seq`.

## Card-ready probe workaround

`src/emulator/mcu.cpp` also contains the CardRAM probe workaround used for reliable Card/C navigation.

During a specific firmware probe, the firmware reads the last CardRAM byte:

    CARD[0x7FFF]

MiniJV880 does not return arbitrary tail data from the card image for that specific probe. Instead it returns the expected identity/probe value, informally referred to as the "Soland" trick.

This prevents Card/C navigation from being blocked or clamped because of unrelated data stored at the end of the CardRAM image.

## Runtime SD backing

Main files:

    src/minijv880.cpp
    src/minijv880.h

Important paths:

    jv880_cardram.bin
    jv880_cardram.tmp
    CARD-RAM/
    CARD-RAM/current.txt
    CARD-RAM/<selected-card>.bin

At startup, MiniJV880 reads `CARD-RAM/current.txt`, validates the selected card image and loads the selected 32768-byte file into `mcu.cardram`.

If the collection workflow is not available, the runtime can fall back to the legacy root-level `jv880_cardram.bin`.

## Dirty tracking and flush

The emulator marks CardRAM dirty when the firmware writes to it.

The runtime exposes:

    FlushCardRAMIfNeeded()

This function checks the dirty flag, takes a stable snapshot of the 32 KB CardRAM buffer using `cardram_seq`, then writes the snapshot back to the active SD-card file.

The write path uses a temporary-file/rename workflow to reduce the risk of corrupting the selected card image.

## HTTP and TFTP management

Main file:

    src/netfileserver.cpp

HTTP endpoints include:

    /cardram
    /cardram-list
    /cardram-select
    /cardram-select-exec
    /cardram-rename
    /cardram-rename-exec
    /cardram-delete
    /cardram-delete-exec
    /cardram.txt

The HTTP layer lists `.bin` files in:

    SD:/CARD-RAM/

It validates card size, writes `current.txt`, and handles rename/delete workflows with prechecks.

TFTP CardRAM upload/download is also handled in `src/netfileserver.cpp`.

TFTP CardRAM transfers are restricted to:

    SD:/CARD-RAM/

and require exactly:

    32768 bytes

Uploads use a temporary file first, followed by rename to the final `.bin` file.

## PC-side tools

Main files:

    tools/minijv880_cardram_tool.py
    tools/minijv880_cardram_gui.py

The CLI and GUI tools are useful for offline inspection and editing of CardRAM images.

They understand the observed 32768-byte CardRAM layout:

    header area
    16 Performance slots
    64 Patch slots
    raw Rhythm area

The PC-side tools are useful but not required by the MiniJV880 runtime.
