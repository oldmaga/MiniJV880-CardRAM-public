MiniJV880pi – SR Robust Build README

MiniJV880pi – SR Robust Build is developed and tested on Raspberry Pi 4 Model B.
Timing, MCU cycles and audio configuration are tuned for this platform.
Other Raspberry Pi models may require manual adjustments.


SR Overlay Toggle Input
In this build the SR overlay is toggled only by the GPIO DATA button raw edge (ButtonPinData).
MIDI button events (including MIDIButtonData) generate normal JV880 UI button presses but do NOT toggle the SR overlay.
If you run a setup with MIDI-only buttons, you still need at least one GPIO button wired to DATA to open/close the SR overlay.

Hardware Pinout Summary
GPIO2: I2C SDA (HD44780 LCD I2C)
GPIO3: I2C SCL (HD44780 LCD I2C)
GPIO4: Debug Serial TX (38400 8N1)
GPIO14: UART TX (reserved for MIDI)
GPIO15: UART RX (reserved for MIDI)

Debug Serial (GPIO4)
Connect GPIO4 -> USB-TTL RX and GND -> GND.
Settings: 38400 baud, 8 data bits, no parity, 1 stop bit, no flow control.
Example:
minicom -D /dev/ttyUSB0 -b 38400 -8 -o

Software Configuration Summary
DATA button is reserved for SR overlay toggle and ButtonActionData is ignored.
Recommended button action: click for all buttons.
MIDI buttons supported but cannot toggle SR overlay.
LCD recommended size: 24x2.
Unused options: SampleRate, IgnoreAllNotesOff, MIDIDumpEnabled, MIDISaveNVRAM, ExpRom, ButtonPinUp/Down/SaveNVRAM.

====================
SD Card Layout
====================

All files must be placed in the BOOT (FAT32) partition of the SD card.

Recommended structure:

/
|-- kernel8-rpi4.img
|-- minijv880.ini
|
|-- jv880_rom1.bin
|-- jv880_rom2.bin
|-- jv880_waverom1.bin
|-- jv880_waverom2.bin
|-- jv880_nvram.bin
|
|-- /roms/
|     |-- SR-JV80-01.bin
|     |-- SR-JV80-02.bin
|     |-- SR-JV80-03.bin
|     |-- ...

Notes:

JV-880 base ROMs MUST be present:
- jv880_rom1.bin
- jv880_rom2.bin
- jv880_waverom1.bin
- jv880_waverom2.bin
- jv880_nvram.bin

If any of these files are missing, the synth will not boot.

SR Expansion ROMs are OPTIONAL:
- Stored inside the /roms folder
- Automatically detected at boot
- Any number of SR files can be present

If the /roms folder is missing or empty:
- The synth still boots normally
- The SR menu will display an error message

SR ROM requirements:
- Must be valid SR-JV80 expansion images
- Size must match expected expansion size
- Invalid files are ignored safely


====================
SR Expansion System
====================

This build introduces a dynamic SR-JV80 expansion management system
with safe hot-swap capability.

Unlike the original MiniJV880 implementation (which used a fixed
ExpRom parameter), this build automatically scans the /roms folder
at boot and builds the SR expansion list dynamically.


Automatic Detection

At boot:

- The /roms directory is scanned
- All valid SR-JV80 images are detected
- Invalid or corrupted files are ignored
- The available SR list is stored internally

No manual configuration is required.


Opening the SR Overlay

Press the DATA GPIO button to open the SR overlay.

The overlay shows:

- All detected SR expansions
- Current selection
- Error messages (if any)


Navigation

Inside the SR overlay:

- Encoder → scroll through SR list
- ENTER → load selected SR
- DATA → close overlay


Safe Hot-Swap Process

When an SR expansion is selected:

1. Audio engine is paused
2. Secondary cores are synchronized
3. Expansion ROM memory is replaced
4. Audio engine is resumed
5. Firmware continues without reboot

The process is fully safe and avoids audio glitches
or system instability.


Error Handling

The system handles all common error cases:

- Missing /roms folder
- Empty /roms folder
- Invalid SR file
- Read failure
- Expansion load failure

In all cases:

- The engine remains stable
- The system does not crash
- Clear feedback is shown on LCD


SR Overlay Requirements

The SR overlay toggle is bound to the GPIO DATA raw edge.

Important:

- MIDIButtonData does NOT toggle the SR overlay
- At least one physical GPIO DATA button is required
  even if all other buttons are controlled via MIDI


Legacy ExpRom Parameter

The ExpRom option in minijv880.ini is no longer used.

SR selection is fully dynamic and controlled by
the SR overlay system.


====================
Controls Overview
====================

This section describes how to operate MiniJV880pi during normal use.

The goal of this build is to behave as a standalone JV-880 hardware unit.


Main Controls

Encoder
- Scroll through parameters and menus
- Scroll SR list when SR overlay is open

ENTER button
- Confirms selections
- Loads the selected SR expansion when SR overlay is open

DATA button (special role)
- Opens the SR overlay
- Closes the SR overlay
- This button is reserved for SR management


Normal JV-880 Buttons

The following buttons behave like the original JV-880 front panel:

- PREVIEW
- LEFT / RIGHT
- TONE SELECT
- PATCH / PERFORMANCE
- EDIT
- SYSTEM
- RHYTHM
- UTILITY
- MUTE
- MONITOR
- COMPARE
- ENTER


SR Overlay Workflow

Typical SR change procedure:

1) Press DATA → SR overlay opens
2) Rotate encoder → select expansion
3) Press ENTER → load expansion
4) Wait ~1 second for safe reload
5) System returns to normal operation


Automatic Play Mode Fix

The original firmware sometimes exits menus in PERFORMANCE mode.

This build automatically detects the current play mode and
injects the required PATCH/PERF button press when needed.

This keeps the instrument in the expected play mode and
avoids user confusion.


MIDI Button Support

Buttons can also be triggered via MIDI CC messages.

However:

- MIDI buttons behave like normal front panel buttons
- MIDI cannot open the SR overlay
- A physical DATA GPIO button is required


Designed Usage Model

This build is designed to be used as a standalone instrument:
- Power on
- Play immediately
- Change SR expansions without reboot
- No keyboard or screen required


=======JV-880 Front Panel Emulation Note========

This build emulates the behaviour of the original JV-880 front panel
button matrix.

However, some differences exist due to hardware configuration and
limitations of the Raspberry Pi based implementation.

For example:
- Some aspects of the Tone Select section are simplified
- Certain hardware interactions are internally emulated

Future versions aim to replicate the full behaviour of the original
JV-880 front panel as closely as possible.

=================================================

====================
Troubleshooting
====================

Display turns on but shows nothing
----------------------------------
Possible causes:
- Wrong I2C address (check LCDI2CAddress in minijv880.ini)
- SDA/SCL wiring swapped (GPIO2 / GPIO3)
- Missing pull-up resistors (common on some I2C backpacks)

Typical fix:
Verify I2C address using an I2C scanner or try 0x27 / 0x3F.


SR menu does not open
---------------------
The SR overlay can ONLY be opened using the DATA GPIO button.

Common causes:
- DATA button not wired
- Wrong GPIO configured for ButtonPinData
- Attempting to use MIDI button instead of GPIO button

Important:
MIDIButtonData does NOT open the SR overlay.


SR menu shows error message
---------------------------
Meaning:
The /roms folder is missing or empty.

Fix:
Create the folder on the SD card:
 /roms/

Add valid SR-JV80 ROM files inside.


Some SR files do not appear in the list
---------------------------------------
Invalid or corrupted SR images are ignored automatically.

Check:
- File is a real SR-JV80 expansion dump
- File size matches expected expansion size


No sound after selecting SR
---------------------------
Wait ~1 second after pressing ENTER.

During SR loading:
- Audio engine pauses
- Expansion memory is replaced
- Engine restarts safely

Interrupting power during this phase is not recommended.


Buttons do not respond
----------------------
Check GPIO conflicts.

Important conflicts:
- GPIO2/3 → reserved for I2C LCD
- GPIO4 → reserved for Debug Serial
- GPIO14/15 → reserved for MIDI UART

Avoid reusing these pins for buttons.


Strange LCD characters or glitches
----------------------------------
Recommended display configuration:
LCDColumns = 24
LCDRows    = 2

Other sizes may work but are not fully tested.


System boots but freezes early
------------------------------
Check that all required JV ROM files are present:

- jv880_rom1.bin
- jv880_rom2.bin
- jv880_waverom1.bin
- jv880_waverom2.bin
- jv880_nvram.bin

The synth cannot start without them.

====================
Project Status / Roadmap
====================

Current status
--------------
This version focuses on stability and real-hardware usability.

Implemented:
- Raspberry Pi 4 optimized build
- HD44780 I2C display support
- Full GPIO button panel (12 buttons)
- MIDI input support
- Rotary encoder navigation
- SR-JV80 expansion loading from SD
- Safe hot-swap SR system
- Debug serial output via GPIO4
- Behaviour modeled on real JV-880 front panel


Known differences vs real JV-880
--------------------------------
This version emulates the behaviour of the original JV-880 front panel,
but some differences remain due to hardware limitations and current
configuration.

Examples:
- Tone Select section behaviour differs slightly
- Some UI shortcuts are simplified
- SR management is implemented as an overlay menu

Future versions aim to replicate the original hardware behaviour
more faithfully.


Planned features
----------------
- Full Tone Select behaviour replication
- Improved SR management and validation
- NVRAM save/restore from Utility menu
- Improved LCD UI rendering
- Additional display support
- General code cleanup and refactoring


Project philosophy
------------------
Goal of this project:
Bring the Roland JV-880 experience to a compact Raspberry Pi system,
while keeping the behaviour as close as possible to the original hardware.


