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

The main PC-side tool manuals are:

- `tools/minijv880_cardram_tool_manual_en.txt`
- `tools/minijv880_cardram_tool_manual_it.txt`
- `tools/minijv880_cardram_gui_manual_en.txt`
- `tools/minijv880_cardram_gui_manual_it.txt`
- `docs/remote-gui.md`
- `docs/remote-gui-it.md`

The CardRAM CLI and CardRAM Manager manuals are plain text files stored in `tools/`.

The Remote GUI manuals are Markdown documents stored in `docs/`, because they are part of the public documentation set and are linked from the documentation index.

## Platform notes

The CardRAM command-line tool and CardRAM GUI are Python 3 tools.

The CardRAM GUI uses Tkinter.

These tools are developed and tested mainly on Linux.

They may work on other desktop platforms if Python 3 and Tkinter are available, but Windows and macOS are not validation targets for this release.

The TFTP helper GUI is Linux-oriented because it is a shell script workflow.

On Windows, use WSL/Linux or adapt the workflow manually.

## TFTP helper and dualboot layout display

The PC-side TFTP helper keeps using the stable remote TFTP name:

    kernel8-rpi4.img

For dualboot-aware firmware, the helper reads:

    /boot-layout.txt

and displays the managed MiniJV880 active, staged and backup kernel paths before kernel staging. This is informational: the TFTP remote name stays unchanged for compatibility, and the firmware maps it to the correct managed MiniJV880 path.

If `/boot-layout.txt` is unavailable, the helper falls back to the legacy singleboot path display.

<!-- MIDI_TOOL_LAB_DOC_START -->
### MiniJV880 MIDI button test tool and MIDI Lab

`tools/minijv880_midi_buttons_test.py` is a PC-side ALSA/amidi helper for testing MiniJV880 MIDI button control from a standard MIDI output port. It does not require Python MIDI libraries, but it does require the `amidi` command from `alsa-utils`.

The tool is organized into two tabs:

- **MIDI Buttons**: a remote-panel style view of the mapped MiniJV880 / JV-880 controls. Normal buttons send a press value followed by a release value. Hold-style buttons such as PREVIEW, TONE SELECT and DATA keep an internal hold state and show it with a small LED. ALL RELEASE / CC 64 is available as a safety command for clearing MIDI-held button states.
- **MIDI Lab**: an experimental area for trying additional MIDI messages without changing the firmware or the INI file.

The MIDI Lab tab currently includes:

- **Generic CC sender**: sends an arbitrary CC number and value on the selected channel. It can send a single value, a press/release tap, hold down value 0, hold up value 127, and ALL RELEASE / PANIC / CC 64.
- **Experimental presets**: four temporary editable slots with Name, Note, CC and Value fields. `Load` copies the preset CC/value into the Generic CC sender. `Send` transmits the preset value directly. The Note field is descriptive only and does not affect the transmitted MIDI bytes.
- **Raw MIDI bytes**: sends 1 to 3 raw hexadecimal bytes, for example `B0 40 00`. SysEx is intentionally excluded from this first MIDI Lab implementation.
- **In-tab logs**: both the MIDI Buttons tab and the MIDI Lab tab contain a log panel. Log messages are mirrored to both views.

Current limitations:

- MIDI Lab presets are temporary and are not saved to disk.
- Raw MIDI is limited to 1-3 bytes and intentionally does not support SysEx yet.
- DATA / CC49 is kept for completeness in the 2.4.0 branch, but the DATA CC event itself has no practical effect. Use DATA dial ◀ / DATA dial ▶ for encoder-style DATA movement and SR overlay toggle for the MiniJV880 SR overlay.
<!-- MIDI_TOOL_LAB_DOC_END -->
