# CardRAM workflow

MiniJV880-CardRAM supports JV-880-style Data Card RAM using SD-card-backed 32 KB `.bin` images.

This lets the user keep multiple Data Card images on the SD card and choose which one should be active.

## CardRAM image size

Each CardRAM image must be exactly:

    32768 bytes

This matches the JV-880/M-256E-style Data Card RAM size.

## Recommended SD-card layout

The recommended CardRAM collection folder is:

    CARD-RAM/

Example:

    CARD-RAM/
    |-- current.txt
    |-- MyCard01.bin
    |-- MyCard02.bin
    |-- Empty.bin

Only `.bin` files directly inside `CARD-RAM/` are part of the normal CardRAM collection workflow.

## Active card selection

The active card is selected by:

    CARD-RAM/current.txt

This file contains the name of the selected `.bin` file.

Example:

    MyCard01.bin

At boot, MiniJV880 reads `CARD-RAM/current.txt`, validates the selected file and loads that 32768-byte card image into the emulated CardRAM buffer.

## Legacy fallback

Older/simple workflows may use a root-level file:

    jv880_cardram.bin

The `CARD-RAM/` collection workflow is recommended, but the legacy file may still be used as a fallback.

## Runtime behavior

During operation, the JV-880 firmware reads and writes the emulated CardRAM as if a real Data Card were present.

When the firmware writes to the CardRAM area, MiniJV880 marks the CardRAM buffer as dirty.

Dirty data is flushed back to the selected SD-card `.bin` image when needed, using a safer temporary-file/rename workflow.

## HTTP CardRAM management

The embedded HTTP server provides CardRAM management pages for local maintenance.

Main workflows include:

- CardRAM status page.
- CardRAM collection list.
- Select card for next boot.
- Rename card image.
- Delete card image.
- Plain text CardRAM status endpoint.

The selection workflow writes:

    CARD-RAM/current.txt

Before changing the selection, MiniJV880 flushes the currently active CardRAM so that recent writes are not lost.

## TFTP CardRAM transfer

TFTP can be used for full CardRAM image transfer.

CardRAM TFTP uploads/downloads are restricted to:

    SD:/CARD-RAM/

Uploaded CardRAM images must be exactly:

    32768 bytes

Uploads use a temporary path first, then rename to the final `.bin` file when complete.

## Creating a new CardRAM image

If no CardRAM image is available, MiniJV880 can generate a new CardRAM file.

The new card can then be initialized/formatted through the normal JV-880 instrument workflow.

## What is not included

The public repository does not include CardRAM images containing patch, performance or rhythm data.

Users must create their own CardRAM images or use only files they are legally allowed to use.
