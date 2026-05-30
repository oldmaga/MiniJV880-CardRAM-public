# Remote GUI

MiniJV880 includes an external PC-side remote-control GUI:

    tools/minijv880_remote_gui.py

This tool runs on the development PC. It is not served by the MiniJV880 embedded HTTP server and is not part of the MiniJV880 firmware runtime.

The goal is to provide a practical remote front panel for testing and maintenance while keeping the embedded HTTP server small and stable.

## System prerequisites

The remote GUI is a PC-side Python tool. It is not compiled into the MiniJV880 firmware and it is not served by the MiniJV880 embedded HTTP server.

Required on the PC:

- Python 3;
- Tkinter support for Python;
- network access to the MiniJV880 HTTP server.

For full LCD/LED live readback, serial monitor support is also recommended:

- `pyserial`;
- a serial adapter connected to the MiniJV880 debug serial output;
- permission to access the serial device, for example `/dev/ttyUSB0`.

On Debian/Ubuntu-style systems the typical packages are:

    sudo apt install python3 python3-tk python3-serial

If `python3-serial` is not available or a virtual environment is used, install `pyserial` in the Python environment used to run the GUI.

The serial monitor is passive: it reads MiniJV880 serial output from the PC side and does not send commands over serial.

Current serial monitor defaults:

- 38400 baud;
- 8 data bits;
- no parity;
- 1 stop bit.

Remote commands are still sent over HTTP.

## Run

From the repository root:

    cd /path/to/Mini-JV880pi
    python3 tools/minijv880_remote_gui.py

Example using the development tree:

    cd /media/roberto/Dep1/GIT-JV880/Mini-JV880pi
    python3 tools/minijv880_remote_gui.py

The GUI opens as a desktop Tkinter application on the PC.

If Tkinter is missing, install the Python Tk package for the system, for example:

    sudo apt install python3-tk

If the serial monitor cannot be started, check that `pyserial` is installed and that the current user can access the serial device.

On many Linux systems this may require membership in the `dialout` group:

    sudo usermod -aG dialout "$USER"

After changing group membership, log out and log back in.

## Connection

The GUI talks to the MiniJV880 over the small existing HTTP endpoints.

Default URL:

    http://192.168.1.50:8080

The URL can be changed in the GUI.

Use the tool only on a trusted local network. Do not expose the MiniJV880 HTTP server to the Internet.

## Main areas

The top area contains:

- connection and HTTP status;
- a single two-line LCD readback display with real cursor position;
- serial monitor controls;
- manual LCD refresh and HTTP fallback controls;
- optional serial log details.

The lower area contains two remote keyboard views:

- MiniJV880 current hardware;
- Original JV-880 panel.

The MiniJV880 current hardware view reflects the practical button layout available on the MiniJV880 unit.

The Original JV-880 panel view is a test-oriented virtual panel intended to make JV-880 manual workflows easier to follow.

## LCD readback

The GUI can read the remote LCD in two ways.

Preferred interactive path:

- passive serial LCD/cursor readback, when the serial monitor is active.

HTTP fallback path:

- `/rlcd.txt` at startup;
- manual `Refresh LCD`;
- event-driven fallback after remote commands when the serial monitor is not active.

The GUI does not use periodic HTTP polling.

When the serial monitor is active, automatic HTTP LCD fallback is skipped.

## LED readback

When the serial monitor starts, the GUI performs one small HTTP read from:

    /rled.txt

This is used to synchronize the initial LED state.

After that, LED/LCD updates should normally come from passive serial events.

The LCD cursor is read from the firmware LCD state, not inferred from text content.
The GUI renders the cursor on the 24-column LCD grid using the cursor row/column/address reported by the MiniJV880.

## Display customization

The `Display...` window provides PC-side customization of the remote LCD rendering.

Available settings include:

- LCD color preset;
- background tone;
- character style;
- text color;
- background/text invert option.

These settings only affect the PC-side GUI display. They do not change the MiniJV880 firmware, the physical LCD, or the data returned by `/rlcd.txt` and serial LCD events.

The dot-matrix renderer is intended to resemble the character LCD more closely. The text-based renderers are useful as alternatives when a clearer desktop display is preferred.

Cursor readback is available through:

    LCDC|ROW=...|COL=...|ENABLED=...|VISIBLE=...|ADDR=...

on the passive serial monitor, and through the `CURSOR:` line returned by `/rlcd.txt`.
The firmware suppresses serial flooding from cursor blink-only changes: serial cursor updates are emitted when LCD text or cursor position changes, not for every blink phase.

## Remote commands

The GUI uses small HTTP remote-control endpoints, including:

    /rraw?a=tap&m=<mask>
    /rraw?a=down&m=<mask>
    /rraw?a=up&m=<mask>
    /renc?d=cw
    /renc?d=ccw
    /rclr

The remote command model is intentionally explicit and small.

The GUI does not serve large UI pages from the MiniJV880 device.

## Important safety note about held buttons

The HTTP LCD endpoint `/rlcd.txt` is a read-only endpoint, but it goes through the normal HTTP path.

Manual `Refresh LCD` should not be used while remote hold gestures are active, such as:

- DATA hold;
- TONE SELECT hold;
- ENTER down / ENTER long while the press is still active.

The GUI avoids automatic HTTP LCD fallback during local hold states, but manual refresh remains a manual diagnostic action.

Use `Clear remote` to release any remote-held state if the GUI and MiniJV880 state become unclear.

## Serial monitor

The serial monitor is passive PC-side readback of MiniJV880 debug serial output.

It is useful for:

- live LCD and cursor tracking;
- LED tracking;
- checking remote command effects;
- keeping the embedded HTTP traffic minimal during interactive testing.

The serial monitor does not send commands over serial. Remote commands still use HTTP.

## Settings persistence

The GUI saves its PC-side settings on close.

Default path:

    ~/.config/minijv880_remote_gui/settings.json

If `XDG_CONFIG_HOME` is set, it is used instead of `~/.config`.

Saved settings include:

- MiniJV880 URL;
- HTTP timeout;
- serial port;
- serial baud rate;
- HTTP fallback setting;
- window geometry;
- Show details state;
- Show safety notes state;
- LCD color preset;
- LCD background tone;
- LCD text color;
- LCD background/text invert option;
- LCD character style.

To reset the GUI settings, close the GUI and remove the settings file:

    rm -f ~/.config/minijv880_remote_gui/settings.json

## Known limitations

The remote GUI is a development and maintenance aid, not a complete replacement for the physical front panel.

The Original JV-880 panel view is intended to help follow original JV-880 workflows, but the MiniJV880 hardware and firmware mapping are not a perfect one-to-one copy of the original instrument.

The HTML remote page and the Python remote GUI have different strengths:

- the HTML page is simple and portable but has no access to the PC serial monitor;
- the Python GUI is better for interactive testing because it can use passive serial LCD/LED readback.

## Troubleshooting

If the GUI cannot contact MiniJV880, check:

- MiniJV880 IP address;
- HTTP port;
- Ethernet connection;
- whether the MiniJV880 HTTP server is enabled.

If the serial monitor does not start, check:

- the serial device path, for example `/dev/ttyUSB0`;
- baud rate;
- user permissions for the serial device;
- whether `pyserial` is installed.

If the LCD does not update automatically during interactive testing, start the serial monitor and verify that MiniJV880 serial LCD events are being received.

If the GUI state looks inconsistent after an interrupted hold gesture, press `Clear remote`.
