#!/usr/bin/env python3
"""
MiniJV880 MIDI Buttons Test Tool

Simple PC-side helper to send MIDI CC button commands to MiniJV880.

It uses the ALSA 'amidi' command, so it does not require Python MIDI
libraries. It needs a visible ALSA raw MIDI output port.

Current MiniJV880 firmware behavior:
  CC value < 64 triggers the button/command.
  CC value >= 64 sends BtnEventNone for normal MIDI buttons.

For the new SR overlay command:
  MIDIButtonSROverlay = CC 62
  recommended test value = 0
"""

import subprocess
import time
import tkinter as tk
from tkinter import ttk, messagebox



def run_cmd(args):
    try:
        return subprocess.run(
            args,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
    except FileNotFoundError:
        return None


def list_amidi_ports():
    proc = run_cmd(["amidi", "-l"])
    if proc is None:
        return [], "ERROR: 'amidi' not found. Install alsa-utils."

    text = (proc.stdout or "") + (proc.stderr or "")
    ports = []

    for line in text.splitlines():
        line = line.rstrip()
        # Typical line:
        # IO  hw:2,0,0  Some MIDI Device MIDI 1
        parts = line.split(None, 2)
        if len(parts) >= 2 and parts[1].startswith("hw:"):
            direction = parts[0]
            port = parts[1]
            desc = parts[2] if len(parts) > 2 else ""
            if "O" in direction or "IO" in direction:
                ports.append((port, f"{port}  {desc}".strip()))

    return ports, text


def send_cc(port, channel_1_based, cc, value):
    channel = max(1, min(16, int(channel_1_based))) - 1
    status = 0xB0 | channel
    data = f"{status:02X} {cc & 0x7F:02X} {value & 0x7F:02X}"

    proc = run_cmd(["amidi", "-p", port, "-S", data])
    if proc is None:
        return False, "ERROR: 'amidi' not found. Install alsa-utils."

    output = (proc.stdout or "") + (proc.stderr or "")
    if proc.returncode != 0:
        return False, output.strip() or f"amidi returned {proc.returncode}"

    return True, data


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("MiniJV880 MIDI Buttons Test")
        self.geometry("860x800")

        # Visual style borrowed from the external remote keyboard:
        # rounded Canvas keys, subtle shadow, compact Tk fonts.
        self.key_bg = "#cfcfcf"
        self.key_active_bg = "#c2c2c2"
        self.key_outline = "#777777"
        self.key_border_width = 2
        self.key_corner_radius = 10
        self.key_highlight = "#eeeeee"
        self.key_drop_shadow = "#a5a5a5"
        self.key_led_off = "#3a2630"
        self.key_led_on = "#ff3030"
        self.key_led_outline_off = "#2a1a22"
        self.key_led_outline_on = "#8a0000"
        self.key_font = ("TkDefaultFont", 9)
        self.key_font_bold = ("TkDefaultFont", 9, "bold")
        self.header_font = ("TkDefaultFont", 11, "bold")
        self.subheader_font = ("TkDefaultFont", 9)

        self._configure_styles()

        self.port_var = tk.StringVar()
        self.channel_var = tk.IntVar(value=1)
        self.value_var = tk.IntVar(value=0)
        self.release_value_var = tk.IntVar(value=127)
        self.tap_delay_ms_var = tk.IntVar(value=80)

        # MIDI Lab experimental controls.
        self.lab_cc_var = tk.IntVar(value=65)
        self.lab_value_var = tk.IntVar(value=0)
        self.lab_release_value_var = tk.IntVar(value=127)
        self.lab_raw_var = tk.StringVar(value="B0 40 00")

        # Non-persistent experimental preset slots for quick MIDI Lab tests.
        self.lab_preset_vars = [
            {
                "name": tk.StringVar(value="Test CC65"),
                "note": tk.StringVar(value="Free test slot"),
                "cc": tk.IntVar(value=65),
                "value": tk.IntVar(value=0),
            },
            {
                "name": tk.StringVar(value="Test CC66"),
                "note": tk.StringVar(value="Free test slot"),
                "cc": tk.IntVar(value=66),
                "value": tk.IntVar(value=0),
            },
            {
                "name": tk.StringVar(value="Test CC67"),
                "note": tk.StringVar(value="Value 127 test"),
                "cc": tk.IntVar(value=67),
                "value": tk.IntVar(value=127),
            },
            {
                "name": tk.StringVar(value="Test CC68"),
                "note": tk.StringVar(value="Free test slot"),
                "cc": tk.IntVar(value=68),
                "value": tk.IntVar(value=0),
            },
        ]

        self._build_ui()
        self.refresh_ports()

    def _configure_styles(self):
        style = ttk.Style(self)
        style.configure("TButton", padding=(4, 2), font=self.key_font)
        style.configure("TLabel", font=self.key_font)
        style.configure("TLabelframe", padding=(4, 3))
        style.configure("TLabelframe.Label", padding=(2, 0), font=self.key_font_bold)
        style.configure("TEntry", padding=(2, 1))
        style.configure("TSpinbox", padding=(2, 1))
        style.configure("TCombobox", padding=(2, 1))
        style.configure("Header.TLabel", font=self.header_font)
        style.configure("Subheader.TLabel", font=self.subheader_font)
        style.configure("Hint.TLabel", font=self.key_font)
        style.configure("TNotebook.Tab", padding=(8, 3), font=self.key_font_bold)

    def _rounded_rect_points(self, x1, y1, x2, y2, radius):
        return [
            x1 + radius, y1,
            x2 - radius, y1,
            x2, y1,
            x2, y1 + radius,
            x2, y2 - radius,
            x2, y2,
            x2 - radius, y2,
            x1 + radius, y2,
            x1, y2,
            x1, y2 - radius,
            x1, y1 + radius,
            x1, y1,
        ]

    def _create_rounded_button_rect(self, canvas, x1, y1, x2, y2):
        radius = min(
            self.key_corner_radius,
            max(1, (x2 - x1) // 2),
            max(1, (y2 - y1) // 2),
        )

        shadow_points = self._rounded_rect_points(x1 + 2, y1 + 2, x2 + 2, y2 + 2, radius)
        canvas.create_polygon(
            shadow_points,
            smooth=True,
            splinesteps=12,
            fill=self.key_drop_shadow,
            outline=self.key_drop_shadow,
            width=1,
        )

        points = self._rounded_rect_points(x1, y1, x2, y2, radius)
        face = canvas.create_polygon(
            points,
            smooth=True,
            splinesteps=12,
            fill=self.key_bg,
            outline=self.key_outline,
            width=self.key_border_width,
        )

        inner_inset = 5
        inner_radius = max(1, radius - inner_inset)
        inner_points = self._rounded_rect_points(
            x1 + inner_inset,
            y1 + inner_inset,
            x2 - inner_inset,
            y2 - inner_inset,
            inner_radius,
        )
        canvas.create_polygon(
            inner_points,
            smooth=True,
            splinesteps=12,
            fill="",
            outline=self.key_highlight,
            width=1,
        )

        return face

    def _panel_button(
        self,
        parent,
        text,
        command,
        row,
        column,
        *,
        width_px=146,
        height_px=84,
        columnspan=1,
        padx=4,
        pady=4,
        font=None,
    ):
        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.cget("background"),
            takefocus=False,
        )
        canvas.grid(
            row=row,
            column=column,
            columnspan=columnspan,
            padx=padx,
            pady=pady,
            sticky="nsew",
        )

        margin = 3
        face = self._create_rounded_button_rect(
            canvas,
            margin,
            margin,
            width_px - margin,
            height_px - margin,
        )

        text_item = canvas.create_text(
            width_px // 2,
            height_px // 2,
            text=text,
            justify="center",
            anchor="center",
            fill="black",
            font=font or self.key_font,
        )

        canvas._midi_face = face
        canvas._midi_text_item = text_item

        def press(_event=None):
            canvas.itemconfigure(face, fill=self.key_active_bg)

        def release(event=None):
            canvas.itemconfigure(face, fill=self.key_bg)
            if event is None:
                command()
                return
            if 0 <= event.x <= width_px and 0 <= event.y <= height_px:
                command()

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")
        return canvas

    def _hold_panel_button(
        self,
        parent,
        name,
        cc,
        row,
        column,
        down_text,
        up_text,
        *,
        width_px=146,
        height_px=84,
        padx=4,
        pady=4,
    ):
        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.cget("background"),
            takefocus=False,
        )
        canvas.grid(row=row, column=column, padx=padx, pady=pady, sticky="nsew")

        margin = 3
        face = self._create_rounded_button_rect(
            canvas,
            margin,
            margin,
            width_px - margin,
            height_px - margin,
        )

        led_w = 30
        led_h = 6
        led_x0 = (width_px - led_w) // 2
        led_y0 = 8
        led_item = canvas.create_rectangle(
            led_x0,
            led_y0,
            led_x0 + led_w,
            led_y0 + led_h,
            fill=self.key_led_off,
            outline=self.key_led_outline_off,
        )

        text_item = canvas.create_text(
            width_px // 2,
            height_px // 2 + 9,
            text=down_text,
            justify="center",
            anchor="center",
            fill="black",
            font=self.key_font_bold,
        )

        canvas._midi_face = face
        canvas._midi_led_item = led_item
        canvas._midi_text_item = text_item

        def press(_event=None):
            canvas.itemconfigure(face, fill=self.key_active_bg)

        def release(event=None):
            canvas.itemconfigure(face, fill=self.key_bg)
            if event is not None and not (0 <= event.x <= width_px and 0 <= event.y <= height_px):
                return
            self.toggle_hold_button(name, cc, canvas, down_text, up_text)

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")

        self._set_hold_button_visual(canvas, False)
        return canvas

    def _set_button_text(self, button, text):
        item = getattr(button, "_midi_text_item", None)
        if item is not None:
            button.itemconfigure(item, text=text)
            return
        try:
            button.configure(text=text)
        except Exception:
            pass

    def show_message_options_help(self):
        selected_tab = ""
        notebook = getattr(self, "notebook", None)

        if notebook is not None:
            try:
                selected_tab = notebook.tab(notebook.select(), "text")
            except Exception:
                selected_tab = ""

        if selected_tab == "MIDI Lab":
            messagebox.showinfo(
                "MIDI Lab help",
                "MIDI Lab is for controlled experiments with extra MIDI messages.\n\n"
                "Generic CC sender:\n"
                "- CC is the controller number, 0-127.\n"
                "- Value is the value sent by Send value and Send tap.\n"
                "- Release is the second value used by Send tap.\n"
                "- Hold down sends value 0.\n"
                "- Hold up sends value 127.\n\n"
                "Experimental presets:\n"
                "- Presets are temporary test slots inside the tool.\n"
                "- Load copies a preset CC/value into the Generic CC sender.\n"
                "- Send transmits that preset value immediately.\n"
                "- Note is only descriptive and does not affect the MIDI bytes.\n"
                "- Presets are not saved to disk in this first version.\n\n"
                "Raw MIDI bytes:\n"
                "- Send raw transmits exactly the hex bytes entered.\n"
                "- This first Lab version allows 1 to 3 bytes only.\n"
                "- SysEx is intentionally excluded for now.\n"
                "- Raw MIDI bypasses the CC, Value, Release and Channel fields.\n\n"
                "Panic:\n"
                "- ALL RELEASE / PANIC / CC 64 clears MiniJV880 MIDI-held button states."
            )
            return

        messagebox.showinfo(
            "MIDI Buttons help",
            "MIDI Buttons is the safe remote-style panel for the mapped MiniJV880 controls.\n\n"
            "Normal buttons:\n"
            "- Send a press value followed by a release value.\n"
            "- Tap ms controls the delay between press and release.\n\n"
            "Hold-style buttons:\n"
            "- PREVIEW, TONE SELECT and DATA use hold/release behavior.\n"
            "- The LED shows whether the hold is currently active.\n"
            "- Value <64 means ON/down; value >=64 means OFF/up.\n\n"
            "DATA and extensions:\n"
            "- DATA dial ◀ / DATA dial ▶ are one-step encoder movements.\n"
            "- DATA / CC49 is kept for completeness; in release 2.4.0 the DATA CC event itself has no practical effect.\n"
            "- SR overlay, ENTER LONG and ALL RELEASE are MiniJV880 extension commands.\n"
            "- ALL RELEASE is a safety/panic command for clearing held MIDI states."
        )

    def _build_ui(self):
        root = ttk.Frame(self, padding=8)
        root.pack(fill="both", expand=True)

        header = ttk.Frame(root)
        header.pack(fill="x", pady=(0, 6))

        ttk.Label(
            header,
            text="MiniJV880 MIDI Buttons",
            style="Header.TLabel",
        ).pack(side="left")

        ttk.Label(
            header,
            text="remote-style CC test panel",
            style="Subheader.TLabel",
        ).pack(side="left", padx=(10, 0))

        top = ttk.LabelFrame(root, text="MIDI output")
        top.pack(fill="x", pady=(0, 6))

        row = ttk.Frame(top, padding=5)
        row.pack(fill="x")

        self.port_combo = ttk.Combobox(row, textvariable=self.port_var, width=58, state="readonly")
        self.port_combo.pack(side="left", fill="x", expand=True)

        ttk.Button(row, text="Refresh ports", command=self.refresh_ports).pack(side="left", padx=(8, 0))

        opts = ttk.LabelFrame(root, text="MIDI message options")
        opts.pack(fill="x", pady=(0, 6))

        opts_row = ttk.Frame(opts, padding=5)
        opts_row.pack(fill="x")

        ttk.Label(opts_row, text="Channel:").pack(side="left")
        ttk.Spinbox(opts_row, from_=1, to=16, textvariable=self.channel_var, width=5).pack(side="left", padx=(4, 16))

        ttk.Label(opts_row, text="Press value:").pack(side="left")
        ttk.Spinbox(opts_row, from_=0, to=127, textvariable=self.value_var, width=5).pack(side="left", padx=(4, 12))

        ttk.Label(opts_row, text="Release value:").pack(side="left")
        ttk.Spinbox(opts_row, from_=0, to=127, textvariable=self.release_value_var, width=5).pack(side="left", padx=(4, 12))

        ttk.Label(opts_row, text="Tap ms:").pack(side="left")
        ttk.Spinbox(opts_row, from_=10, to=1000, textvariable=self.tap_delay_ms_var, width=6).pack(side="left", padx=(4, 12))

        ttk.Button(
            opts_row,
            text="Help tab",
            command=self.show_message_options_help,
        ).pack(side="left")

        self.notebook = ttk.Notebook(root)
        self.notebook.pack(fill="both", expand=True, pady=(0, 6))

        buttons_tab = ttk.Frame(self.notebook, padding=4)
        lab_tab = ttk.Frame(self.notebook, padding=4)

        self.notebook.add(buttons_tab, text="MIDI Buttons")
        self.notebook.add(lab_tab, text="MIDI Lab")

        panel_w = 146
        panel_h = 84
        hold_h = panel_h
        panel_pad = 3

        def add_tap_button(parent, row, col, label, cc):
            return self._panel_button(
                parent,
                f"{label}\nCC {cc}",
                lambda c=cc, n=label: self.send_tap(c, n),
                row,
                col,
                width_px=panel_w,
                height_px=panel_h,
                padx=panel_pad,
                pady=panel_pad,
                font=self.key_font_bold,
            )

        def add_single_button(parent, row, col, label, cc):
            return self._panel_button(
                parent,
                f"{label}\nCC {cc}",
                lambda c=cc, n=label: self.send_single(c, n),
                row,
                col,
                width_px=panel_w,
                height_px=panel_h,
                padx=panel_pad,
                pady=panel_pad,
                font=self.key_font_bold,
            )

        def add_hold_button(parent, row, col, name, cc, down_text, up_text):
            return self._hold_panel_button(
                parent,
                name,
                cc,
                row,
                col,
                down_text,
                up_text,
                width_px=panel_w,
                height_px=hold_h,
                padx=panel_pad,
                pady=panel_pad,
            )

        def add_command_button(parent, row, col, text, command):
            return self._panel_button(
                parent,
                text,
                command,
                row,
                col,
                width_px=panel_w,
                height_px=panel_h,
                padx=panel_pad,
                pady=panel_pad,
                font=self.key_font_bold,
            )

        main_board = ttk.LabelFrame(buttons_tab, text="JV-880 / MiniJV880 main switch board")
        main_board.pack(fill="x", pady=(0, 6))

        main_grid = ttk.Frame(main_board, padding=5)
        main_grid.pack(anchor="center")

        add_tap_button(main_grid, 0, 0, "PATCH/PERFORM", 51)
        add_tap_button(main_grid, 0, 1, "EDIT", 52)
        add_tap_button(main_grid, 0, 2, "SYSTEM", 53)
        add_tap_button(main_grid, 0, 3, "RHYTHM", 54)
        add_tap_button(main_grid, 0, 4, "UTILITY", 55)

        self.tone_select_toggle_btn = add_hold_button(
            main_grid,
            1,
            0,
            "TONE SELECT",
            50,
            "TONE SELECT\nPARAM SHIFT\nCC 50",
            "TONE SELECT\nPARAM SHIFT\nCC 50",
        )
        add_tap_button(main_grid, 1, 1, "TONE SW 1\nMUTE", 56)
        add_tap_button(main_grid, 1, 2, "TONE SW 2\nMONITOR", 57)
        add_tap_button(main_grid, 1, 3, "TONE SW 3\nINFO / COMPARE", 58)
        add_tap_button(main_grid, 1, 4, "TONE SW 4\nENTER", 59)

        helpers = ttk.LabelFrame(buttons_tab, text="Cursor / DATA dial / ENTER LONG")
        helpers.pack(fill="x", pady=(0, 6))

        helpers_grid = ttk.Frame(helpers, padding=5)
        helpers_grid.pack(anchor="center")

        add_tap_button(helpers_grid, 0, 0, "CURSOR ◀", 47)
        add_tap_button(helpers_grid, 0, 1, "CURSOR ▶", 48)
        add_tap_button(helpers_grid, 0, 2, "DATA dial ◀", 61)
        add_tap_button(helpers_grid, 0, 3, "DATA dial ▶", 60)
        add_tap_button(helpers_grid, 0, 4, "ENTER LONG", 63)

        ext = ttk.LabelFrame(buttons_tab, text="MiniJV880 extension / hold safety")
        ext.pack(fill="x", pady=(0, 6))

        ext_grid = ttk.Frame(ext, padding=5)
        ext_grid.pack(anchor="center")

        self.preview_toggle_btn = add_hold_button(
            ext_grid,
            0,
            0,
            "PREVIEW",
            46,
            "PREVIEW HOLD\nCC 46",
            "PREVIEW RELEASE\nCC 46",
        )

        self.data_toggle_btn = add_hold_button(
            ext_grid,
            0,
            1,
            "DATA",
            49,
            "DATA HOLD\nCC 49",
            "DATA RELEASE\nCC 49",
        )

        add_single_button(ext_grid, 0, 2, "SR overlay toggle", 62)

        self._panel_button(
            ext_grid,
            "ALL RELEASE\nheld keys\nCC 64",
            self.release_all_holds,
            0,
            3,
            width_px=panel_w,
            height_px=hold_h,
            padx=panel_pad,
            pady=panel_pad,
            font=self.key_font_bold,
        )

        self.log_views = []

        buttons_log_frame = ttk.LabelFrame(buttons_tab, text="Log")
        buttons_log_frame.pack(fill="both", expand=True, pady=(0, 0))

        self.buttons_log = tk.Text(
            buttons_log_frame,
            height=4,
            wrap="word",
            font=("TkFixedFont", 10),
        )
        self.buttons_log.pack(fill="both", expand=True, padx=6, pady=6)
        self.log_views.append(self.buttons_log)

        # --------------------------------------------------------------
        # MIDI Lab: generic sender for experimental CC/raw messages.
        # --------------------------------------------------------------
        lab_cc = ttk.LabelFrame(lab_tab, text="Generic CC sender")
        lab_cc.pack(fill="x", pady=(0, 6))

        lab_cc_row = ttk.Frame(lab_cc, padding=5)
        lab_cc_row.pack(fill="x")

        ttk.Label(lab_cc_row, text="CC:").pack(side="left")
        ttk.Spinbox(
            lab_cc_row,
            from_=0,
            to=127,
            textvariable=self.lab_cc_var,
            width=5,
        ).pack(side="left", padx=(4, 12))

        ttk.Label(lab_cc_row, text="Value:").pack(side="left")
        ttk.Spinbox(
            lab_cc_row,
            from_=0,
            to=127,
            textvariable=self.lab_value_var,
            width=5,
        ).pack(side="left", padx=(4, 12))

        ttk.Label(lab_cc_row, text="Release:").pack(side="left")
        ttk.Spinbox(
            lab_cc_row,
            from_=0,
            to=127,
            textvariable=self.lab_release_value_var,
            width=5,
        ).pack(side="left", padx=(4, 12))

        ttk.Label(
            lab_cc_row,
            text="Use the buttons below to send the selected CC.",
            style="Hint.TLabel",
        ).pack(side="left")

        lab_cc_buttons = ttk.Frame(lab_cc, padding=(5, 0, 5, 5))
        lab_cc_buttons.pack(anchor="center")

        add_command_button(
            lab_cc_buttons,
            0,
            0,
            "SEND\nVALUE",
            self.send_lab_cc_value,
        )

        add_command_button(
            lab_cc_buttons,
            0,
            1,
            "SEND\nTAP",
            self.send_lab_cc_tap,
        )

        add_command_button(
            lab_cc_buttons,
            0,
            2,
            "HOLD\nDOWN\nvalue 0",
            lambda: self.send_lab_cc_fixed_value(0, "lab-hold-down"),
        )

        add_command_button(
            lab_cc_buttons,
            0,
            3,
            "HOLD\nUP\nvalue 127",
            lambda: self.send_lab_cc_fixed_value(127, "lab-hold-up"),
        )

        add_command_button(
            lab_cc_buttons,
            0,
            4,
            "ALL RELEASE\nPANIC\nCC 64",
            self.release_all_holds,
        )

        lab_presets = ttk.LabelFrame(lab_tab, text="Experimental presets")
        lab_presets.pack(fill="x", pady=(0, 6))

        lab_presets_grid = ttk.Frame(lab_presets, padding=5)
        lab_presets_grid.pack(fill="x")

        ttk.Label(lab_presets_grid, text="Slot").grid(row=0, column=0, padx=(0, 6), pady=(0, 4), sticky="w")
        ttk.Label(lab_presets_grid, text="Name").grid(row=0, column=1, padx=(0, 6), pady=(0, 4), sticky="w")
        ttk.Label(lab_presets_grid, text="Note").grid(row=0, column=2, padx=(0, 6), pady=(0, 4), sticky="w")
        ttk.Label(lab_presets_grid, text="CC").grid(row=0, column=3, padx=(0, 6), pady=(0, 4), sticky="w")
        ttk.Label(lab_presets_grid, text="Value").grid(row=0, column=4, padx=(0, 6), pady=(0, 4), sticky="w")

        for index, preset in enumerate(self.lab_preset_vars):
            row_index = index + 1

            ttk.Label(
                lab_presets_grid,
                text=str(index + 1),
            ).grid(row=row_index, column=0, padx=(0, 6), pady=2, sticky="w")

            ttk.Entry(
                lab_presets_grid,
                textvariable=preset["name"],
                width=16,
            ).grid(row=row_index, column=1, padx=(0, 6), pady=2, sticky="ew")

            ttk.Entry(
                lab_presets_grid,
                textvariable=preset["note"],
                width=32,
            ).grid(row=row_index, column=2, padx=(0, 6), pady=2, sticky="ew")

            ttk.Spinbox(
                lab_presets_grid,
                from_=0,
                to=127,
                textvariable=preset["cc"],
                width=5,
            ).grid(row=row_index, column=3, padx=(0, 6), pady=2, sticky="w")

            ttk.Spinbox(
                lab_presets_grid,
                from_=0,
                to=127,
                textvariable=preset["value"],
                width=5,
            ).grid(row=row_index, column=4, padx=(0, 6), pady=2, sticky="w")

            ttk.Button(
                lab_presets_grid,
                text="Load",
                command=lambda i=index: self.load_lab_preset(i),
            ).grid(row=row_index, column=5, padx=(4, 4), pady=2, sticky="ew")

            ttk.Button(
                lab_presets_grid,
                text="Send",
                command=lambda i=index: self.send_lab_preset(i),
            ).grid(row=row_index, column=6, padx=(0, 0), pady=2, sticky="ew")

        lab_presets_grid.columnconfigure(1, weight=1)
        lab_presets_grid.columnconfigure(2, weight=2)

        lab_raw = ttk.LabelFrame(lab_tab, text="Raw MIDI bytes")
        lab_raw.pack(fill="x", pady=(0, 6))

        lab_raw_row = ttk.Frame(lab_raw, padding=5)
        lab_raw_row.pack(fill="x")

        ttk.Label(lab_raw_row, text="Hex bytes:").pack(side="left")
        ttk.Entry(
            lab_raw_row,
            textvariable=self.lab_raw_var,
            width=24,
        ).pack(side="left", padx=(4, 8))

        ttk.Button(
            lab_raw_row,
            text="Send raw",
            command=self.send_lab_raw_hex,
        ).pack(side="left", padx=(0, 10))

        ttk.Label(
            lab_raw_row,
            text="1-3 hex bytes only; no SysEx.",
            style="Hint.TLabel",
        ).pack(side="left")

        lab_log_frame = ttk.LabelFrame(lab_tab, text="Log")
        lab_log_frame.pack(fill="both", expand=True, pady=(6, 0))

        self.lab_log = tk.Text(
            lab_log_frame,
            height=8,
            wrap="word",
            font=("TkFixedFont", 10),
        )
        self.lab_log.pack(fill="both", expand=True, padx=6, pady=6)
        self.log_views.append(self.lab_log)

        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def log_line(self, text):
        print(text, flush=True)

        for log_view in getattr(self, "log_views", []):
            log_view.insert("end", text + "\n")
            log_view.see("end")

    def refresh_ports(self):
        ports, raw = list_amidi_ports()

        self.log_line("=== amidi -l ===")
        self.log_line(raw.strip() if raw.strip() else "(no output)")

        self.ports = ports
        labels = []
        for port, name in ports:
            name = name.strip()
            if name.startswith(port):
                labels.append(name)
            else:
                labels.append(f"{port}  {name}")
        self.port_combo["values"] = labels

        if not ports:
            self.port_combo.set("")
            self.log_line("No ALSA raw MIDI output ports found.")
            self.log_line("If MiniJV880 does not appear here, the PC currently has no MIDI path to it.")
            return

        selected = 0
        for i, (port, name) in enumerate(ports):
            item = f"{port} {name}"
            if port == "hw:4,0,0" or "UM-ONE" in item:
                selected = i
                break

        self.port_combo.current(selected)
        self.log_line(f"Selected: {labels[selected]}")
        self.log_line(f"Selected MIDI port resolved as: {self.selected_port()}")

    def selected_port(self):
        # Prefer the visible combobox value. This avoids stale/current-index
        # problems when several ALSA raw MIDI ports are present before UM-ONE.
        value = self.port_combo.get().strip()
        if value:
            first = value.split()[0].strip()
            if first.startswith("hw:"):
                return first

        idx = self.port_combo.current()
        if idx < 0 or idx >= len(self.ports):
            return None
        return self.ports[idx][0]

    def send_one_value(self, cc, name, value, label):
        port = self.selected_port()
        if not port:
            messagebox.showerror("No MIDI port", "No ALSA raw MIDI output port selected.")
            return False

        channel = int(self.channel_var.get())

        status = 0xB0 | ((channel - 1) & 0x0F)
        data = f"{status:02X} {cc:02X} {value:02X}"
        self.log_line(f"RUN {label} {name}: amidi -p {port} -S '{data}'")

        ok, result = send_cc(port, channel, cc, value)
        if ok:
            self.log_line(f"SENT {label} {name}: port={port} ch={channel} CC={cc} value={value} bytes={data}")
            return True

        self.log_line(f"ERROR sending {label} {name}: {result}")
        messagebox.showerror("amidi error", result)
        return False

    def _set_hold_button_visual(self, button, active):
        face = getattr(button, "_midi_face", None)
        led_item = getattr(button, "_midi_led_item", None)

        if face is None:
            return

        button._midi_hold_active = bool(active)

        # Hold state is indicated by the LED only; keep the key face neutral.
        button.itemconfigure(face, fill=self.key_bg)

        if led_item is not None:
            button.itemconfigure(
                led_item,
                fill=self.key_led_on if active else self.key_led_off,
                outline=self.key_led_outline_on if active else self.key_led_outline_off,
            )

    def _reset_hold_toggle_button(self, name, button_attr, text):
        if not hasattr(self, "hold_state"):
            self.hold_state = {}

        self.hold_state[name] = False

        button = getattr(self, button_attr, None)
        if button is not None:
            self._set_button_text(button, text)
            self._set_hold_button_visual(button, False)

    def release_all_holds(self):
        # Safety command: ask the firmware to clear all MIDI-held keys/timers,
        # then also send individual releases for compatibility and visibility.
        commands = [
            ("ALL RELEASE", 64, 0, "command"),
            ("DATA", 49, 127, "hold-up"),
            ("PREVIEW", 46, 127, "hold-up"),
            ("TONE SELECT", 50, 127, "hold-up"),
            ("ALL RELEASE", 64, 0, "command"),
        ]

        ok = True
        for index, (name, cc, value, label) in enumerate(commands):
            result = self.send_one_value(cc, name, value, label)
            ok = bool(result) and ok

            if index + 1 < len(commands):
                time.sleep(0.08)

        self._reset_hold_toggle_button(
            "PREVIEW",
            "preview_toggle_btn",
            "PREVIEW HOLD\nCC 46",
        )
        self._reset_hold_toggle_button(
            "TONE SELECT",
            "tone_select_toggle_btn",
            "TONE SELECT\nPARAM SHIFT\nCC 50",
        )
        self._reset_hold_toggle_button(
            "DATA",
            "data_toggle_btn",
            "DATA HOLD\nCC 49",
        )

        return ok

    def on_close(self):
        try:
            self.release_all_holds()
        finally:
            self.destroy()

    def toggle_hold_button(self, name, cc, button, down_text, up_text):
        if not hasattr(self, "hold_state"):
            self.hold_state = {}

        is_down = self.hold_state.get(name, False)

        if is_down:
            if self.send_one_value(cc, name, 127, "hold-up"):
                self.hold_state[name] = False
                self._set_button_text(button, down_text)
                self._set_hold_button_visual(button, False)
        else:
            if self.send_one_value(cc, name, 0, "hold-down"):
                self.hold_state[name] = True
                self._set_button_text(button, up_text)
                self._set_hold_button_visual(button, True)

    def _bounded_int_from_var(self, var, name, minimum=0, maximum=127):
        try:
            value = int(var.get())
        except Exception:
            messagebox.showerror("Invalid value", f"{name} must be an integer.")
            return None

        if value < minimum or value > maximum:
            messagebox.showerror("Invalid value", f"{name} must be between {minimum} and {maximum}.")
            return None

        return value

    def _lab_cc(self):
        return self._bounded_int_from_var(self.lab_cc_var, "CC number", 0, 127)

    def _lab_value(self):
        return self._bounded_int_from_var(self.lab_value_var, "Value", 0, 127)

    def _lab_release_value(self):
        return self._bounded_int_from_var(self.lab_release_value_var, "Release value", 0, 127)

    def send_lab_cc_value(self):
        cc = self._lab_cc()
        value = self._lab_value()
        if cc is None or value is None:
            return False

        return self.send_one_value(cc, f"LAB CC {cc}", value, "lab-value")

    def send_lab_cc_fixed_value(self, value, label):
        cc = self._lab_cc()
        if cc is None:
            return False

        return self.send_one_value(cc, f"LAB CC {cc}", int(value), label)

    def send_lab_cc_tap(self):
        cc = self._lab_cc()
        press_value = self._lab_value()
        release_value = self._lab_release_value()

        if cc is None or press_value is None or release_value is None:
            return False

        delay_ms = max(10, int(self.tap_delay_ms_var.get()))

        if self.send_one_value(cc, f"LAB CC {cc}", press_value, "lab-press"):
            self.after(
                delay_ms,
                lambda c=cc, v=release_value: self.send_one_value(
                    c,
                    f"LAB CC {c}",
                    v,
                    "lab-release",
                ),
            )
            return True

        return False

    def _lab_preset_values(self, index):
        if index < 0 or index >= len(self.lab_preset_vars):
            messagebox.showerror("Invalid preset", "Preset index is out of range.")
            return None

        preset = self.lab_preset_vars[index]
        name = preset["name"].get().strip() or f"Preset {index + 1}"
        note = preset["note"].get().strip()

        cc = self._bounded_int_from_var(preset["cc"], f"Preset {index + 1} CC", 0, 127)
        value = self._bounded_int_from_var(preset["value"], f"Preset {index + 1} value", 0, 127)

        if cc is None or value is None:
            return None

        return name, note, cc, value

    def load_lab_preset(self, index):
        values = self._lab_preset_values(index)
        if values is None:
            return False

        name, note, cc, value = values

        self.lab_cc_var.set(cc)
        self.lab_value_var.set(value)

        note_text = f" note='{note}'" if note else ""
        self.log_line(
            f"LOADED lab preset {index + 1}: name='{name}' CC={cc} value={value}{note_text}"
        )
        return True

    def send_lab_preset(self, index):
        values = self._lab_preset_values(index)
        if values is None:
            return False

        name, note, cc, value = values

        label = f"LAB preset {index + 1} {name}"
        if note:
            label = f"{label} - {note}"

        return self.send_one_value(
            cc,
            label,
            value,
            "lab-preset",
        )

    def send_lab_raw_hex(self):
        port = self.selected_port()
        if not port:
            messagebox.showerror("No MIDI port", "No ALSA raw MIDI output port selected.")
            return False

        text = self.lab_raw_var.get().strip().replace(",", " ")
        parts = [part.strip() for part in text.split() if part.strip()]

        if not 1 <= len(parts) <= 3:
            messagebox.showerror(
                "Invalid raw MIDI",
                "Enter 1 to 3 hex bytes only. SysEx is intentionally excluded for now.",
            )
            return False

        values = []
        try:
            for part in parts:
                if part.lower().startswith("0x"):
                    part = part[2:]
                if not part:
                    raise ValueError("empty byte")
                value = int(part, 16)
                if value < 0 or value > 255:
                    raise ValueError("byte out of range")
                values.append(value)
        except Exception:
            messagebox.showerror(
                "Invalid raw MIDI",
                "Use hex bytes like: B0 40 00",
            )
            return False

        data = " ".join(f"{value:02X}" for value in values)
        self.log_line(f"RUN raw MIDI: amidi -p {port} -S '{data}'")

        proc = run_cmd(["amidi", "-p", port, "-S", data])
        if proc is None:
            messagebox.showerror("amidi error", "ERROR: 'amidi' not found. Install alsa-utils.")
            return False

        output = (proc.stdout or "") + (proc.stderr or "")
        if proc.returncode != 0:
            message = output.strip() or f"amidi returned {proc.returncode}"
            self.log_line(f"ERROR sending raw MIDI: {message}")
            messagebox.showerror("amidi error", message)
            return False

        self.log_line(f"SENT raw MIDI: port={port} bytes={data}")
        return True

    def send_single(self, cc, name):
        value = int(self.value_var.get())
        self.send_one_value(cc, name, value, "single")

    def send_tap(self, cc, name):
        press_value = int(self.value_var.get())
        release_value = int(self.release_value_var.get())
        delay_ms = max(10, int(self.tap_delay_ms_var.get()))

        if self.send_one_value(cc, name, press_value, "press"):
            self.after(delay_ms, lambda: self.send_one_value(cc, name, release_value, "release"))


if __name__ == "__main__":
    App().mainloop()
