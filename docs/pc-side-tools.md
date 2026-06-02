# PC-side tools

MiniJV880-CardRAM includes PC-side helper tools.

These tools run on the development PC. They are not MiniJV880 firmware components and are not required by the MiniJV880 firmware at boot.

## Main tools

Main PC-side tools:

    tools/minijv880_cardram_tool.py
    tools/minijv880_cardram_gui.py
tools/minijv880_remote_gui.py
    tools/minijv880_tftp_gui.sh
    tools/minijv880_tftp_put.py

## CardRAM command-line tool

The command-line CardRAM tool is:

    tools/minijv880_cardram_tool.py

It can inspect, compare and modify CardRAM `.bin` images offline.

It understands the observed 32768-byte CardRAM layout:

    header area
    16 Performance slots
    64 Patch slots
    raw Rhythm area

It includes operations for:

- listing Performance and Patch slots;
- comparing CardRAM images;
- copying, moving, swapping and clearing slots;
- editing slot names;
- exporting and importing raw `.patchslot` files;
- rhythm raw-block inspection/copy workflows;
- generating reports.

Some experimental SysEx/CardRAM research commands may exist in the private development line. Not all experimental research material is part of the public-clean repository.

## CardRAM GUI

The Tkinter CardRAM GUI is:

    tools/minijv880_cardram_gui.py

Run it with:

    python3 tools/minijv880_cardram_gui.py

The GUI provides a visual workflow for:

- loading a base/working CardRAM image;
- loading source CardRAM images;
- copying Performance and Patch slots;
- moving and swapping slots;
- tracking changed items;
- saving a working card;
- generating reports;
- handling the raw Rhythm area;
- importing/exporting `.patchslot` files.

The GUI is useful for preparing CardRAM images on a PC before copying them to the SD card.

## Remote GUI

The external Remote GUI is:

    tools/minijv880_remote_gui.py

Run it with:

    python3 tools/minijv880_remote_gui.py

The Remote GUI provides a PC-side virtual front panel for development, maintenance and interactive testing.

It includes:

- MiniJV880 current hardware button view;
- Original JV-880-oriented virtual panel view;
- remote button tap/down/up commands over the small HTTP endpoints;
- encoder clockwise/counter-clockwise remote commands;
- LCD readback through `/rlcd.txt` and passive serial monitoring;
- LED readback support;
- cursor tracking and dot-matrix LCD rendering;
- display customization for the PC-side LCD view;
- remote hold gesture support for workflows such as PREVIEW or DATA hold.

The Remote GUI is not compiled into the firmware and is not served by the embedded HTTP server. It runs on the PC and talks to MiniJV880 through the small technical HTTP endpoints. For full live LCD/LED readback, a passive serial monitor using `pyserial` is recommended.

See also:

    docs/remote-gui.md

## TFTP helper tools

The TFTP helper files are:

    tools/minijv880_tftp_gui.sh
    tools/minijv880_tftp_put.py

Run the TFTP helper GUI with:

    tools/minijv880_tftp_gui.sh

Or specify the MiniJV880 IP address explicitly:

    MINIJV880_HOST=192.168.1.50 tools/minijv880_tftp_gui.sh

The TFTP helper workflow is intended for local Ethernet maintenance.

## Manuals

Detailed tool usage is documented in:

    tools/minijv880_cardram_tool_manual_en.txt
    tools/minijv880_cardram_tool_manual_it.txt
    tools/minijv880_tftp_helper_manual_en_v4.txt
    tools/minijv880_tftp_helper_manual_it_v4.txt

## Platform notes

The CardRAM command-line tool and CardRAM GUI are Python 3 tools.

The CardRAM GUI uses Tkinter.

These tools are developed and tested mainly on Linux.

They may work on other desktop platforms if Python 3 and Tkinter are available, but Windows and macOS are not validation targets for this release.

The TFTP helper GUI is Linux-oriented because it is a shell script workflow.

On Windows, use WSL/Linux or adapt the workflow manually.
