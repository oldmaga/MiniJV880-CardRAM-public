#!/usr/bin/env python3
"""
MiniJV880 External Remote GUI prototype.

PC-side Tkinter remote control.
No polling by default.
Uses small MiniJV880 HTTP endpoints only.

Current safe scope:
- LCD readback from /rlcd.txt with firmware-side ASCII glyph mapping
- one-shot LED readback from /rled.txt
- passive serial LCD/LED readback
- DATA tap/down/up
- ENTER tap/down/up/long
- UTILITY tap
- PATCH/PERF tap
- encoder CW/CCW
- event-driven optional LCD refresh after remote commands
"""

from __future__ import annotations

import errno
import fcntl
import json
import os
import shutil
import signal
import subprocess
import termios
import threading
import time
import tty
from pathlib import Path
import tkinter as tk
from tkinter import ttk, font as tkfont
from urllib import request, parse, error

try:
    import serial  # type: ignore
except Exception:
    serial = None


DEFAULT_BASE_URL = "http://192.168.1.50:8080"

MASK = {
    "LEFT": "0x00000001",
    "RIGHT": "0x00000002",
    "TONESEL": "0x00000004",
    "TONE1": "0x00000008",
    "DATA": "0x00000010",
    "TONE2": "0x00000020",
    "TONE3": "0x00000040",
    "ENTER": "0x00000080",      # TONE SW4 / ENTER
    "UTILITY": "0x00000100",
    "PREVIEW": "0x00000200",
    "PATCHPERF": "0x00000400",
    "EDIT": "0x00000800",
    "SYSTEM": "0x00001000",
    "RHYTHM": "0x00002000",
}


class MiniJV880RemoteGUI:
    LCD_DOT_GLYPHS = {
        " ": ("00000", "00000", "00000", "00000", "00000", "00000", "00000"),
        "!": ("00100", "00100", "00100", "00100", "00100", "00000", "00100"),
        "\"": ("01010", "01010", "01010", "00000", "00000", "00000", "00000"),
        "#": ("01010", "01010", "11111", "01010", "11111", "01010", "01010"),
        "$": ("00100", "01111", "10100", "01110", "00101", "11110", "00100"),
        "%": ("11001", "11010", "00100", "01000", "10110", "00110", "00000"),
        "&": ("01100", "10010", "10100", "01000", "10101", "10010", "01101"),
        "'": ("00100", "00100", "01000", "00000", "00000", "00000", "00000"),
        "(": ("00010", "00100", "01000", "01000", "01000", "00100", "00010"),
        ")": ("01000", "00100", "00010", "00010", "00010", "00100", "01000"),
        "*": ("00000", "00100", "10101", "01110", "10101", "00100", "00000"),
        "+": ("00000", "00100", "00100", "11111", "00100", "00100", "00000"),
        ",": ("00000", "00000", "00000", "00000", "00110", "00100", "01000"),
        "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
        ".": ("00000", "00000", "00000", "00000", "00000", "01100", "01100"),
        "/": ("00001", "00010", "00100", "01000", "10000", "00000", "00000"),
        "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
        "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
        "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
        "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
        "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
        "5": ("11111", "10000", "11110", "00001", "00001", "10001", "01110"),
        "6": ("00110", "01000", "10000", "11110", "10001", "10001", "01110"),
        "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
        "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
        "9": ("01110", "10001", "10001", "01111", "00001", "00010", "01100"),
        ":": ("00000", "01100", "01100", "00000", "01100", "01100", "00000"),
        ";": ("00000", "01100", "01100", "00000", "01100", "00100", "01000"),
        "<": ("00000", "00100", "01000", "11111", "01000", "00100", "00000"),
        "=": ("00000", "00000", "11111", "00000", "11111", "00000", "00000"),
        ">": ("00000", "00100", "00010", "11111", "00010", "00100", "00000"),
        "?": ("01110", "10001", "00001", "00010", "00100", "00000", "00100"),
        "@": ("01110", "10001", "10111", "10101", "10111", "10000", "01110"),
        "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
        "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
        "C": ("01110", "10001", "10000", "10000", "10000", "10001", "01110"),
        "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
        "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
        "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
        "G": ("01110", "10001", "10000", "10111", "10001", "10001", "01110"),
        "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
        "I": ("01110", "00100", "00100", "00100", "00100", "00100", "01110"),
        "J": ("00111", "00010", "00010", "00010", "00010", "10010", "01100"),
        "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
        "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
        "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
        "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
        "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
        "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
        "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
        "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
        "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
        "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
        "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
        "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
        "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
        "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
        "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
        "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
        "[": ("01110", "01000", "01000", "01000", "01000", "01000", "01110"),
        "\\": ("10000", "01000", "00100", "00010", "00001", "00000", "00000"),
        "]": ("01110", "00010", "00010", "00010", "00010", "00010", "01110"),
        "^": ("00100", "01010", "10001", "00000", "00000", "00000", "00000"),
        "_": ("00000", "00000", "00000", "00000", "00000", "00000", "11111"),
        "`": ("01000", "00100", "00010", "00000", "00000", "00000", "00000"),
        "a": ("00000", "00000", "01110", "00001", "01111", "10001", "01111"),
        "b": ("10000", "10000", "10110", "11001", "10001", "10001", "11110"),
        "c": ("00000", "00000", "01110", "10000", "10000", "10001", "01110"),
        "d": ("00001", "00001", "01101", "10011", "10001", "10001", "01111"),
        "e": ("00000", "00000", "01110", "10001", "11111", "10000", "01110"),
        "f": ("00110", "01001", "01000", "11100", "01000", "01000", "01000"),
        "g": ("00000", "00000", "01111", "10001", "01111", "00001", "01110"),
        "h": ("10000", "10000", "10110", "11001", "10001", "10001", "10001"),
        "i": ("00100", "00000", "01100", "00100", "00100", "00100", "01110"),
        "j": ("00010", "00000", "00110", "00010", "00010", "10010", "01100"),
        "k": ("10000", "10000", "10010", "10100", "11000", "10100", "10010"),
        "l": ("01100", "00100", "00100", "00100", "00100", "00100", "01110"),
        "m": ("00000", "00000", "11010", "10101", "10101", "10001", "10001"),
        "n": ("00000", "00000", "10110", "11001", "10001", "10001", "10001"),
        "o": ("00000", "00000", "01110", "10001", "10001", "10001", "01110"),
        "p": ("00000", "00000", "11110", "10001", "11110", "10000", "10000"),
        "q": ("00000", "00000", "01111", "10001", "01111", "00001", "00001"),
        "r": ("00000", "00000", "10110", "11001", "10000", "10000", "10000"),
        "s": ("00000", "00000", "01111", "10000", "01110", "00001", "11110"),
        "t": ("01000", "01000", "11100", "01000", "01000", "01001", "00110"),
        "u": ("00000", "00000", "10001", "10001", "10001", "10011", "01101"),
        "v": ("00000", "00000", "10001", "10001", "10001", "01010", "00100"),
        "w": ("00000", "00000", "10001", "10001", "10101", "10101", "01010"),
        "x": ("00000", "00000", "10001", "01010", "00100", "01010", "10001"),
        "y": ("00000", "00000", "10001", "10001", "01111", "00001", "01110"),
        "z": ("00000", "00000", "11111", "00010", "00100", "01000", "11111"),
        "{": ("00010", "00100", "00100", "01000", "00100", "00100", "00010"),
        "|": ("00100", "00100", "00100", "00100", "00100", "00100", "00100"),
        "}": ("01000", "00100", "00100", "00010", "00100", "00100", "01000"),
        "~": ("00000", "00000", "01000", "10101", "00010", "00000", "00000"),
        "←": ("00000", "00100", "01000", "11111", "01000", "00100", "00000"),
        "→": ("00000", "00100", "00010", "11111", "00010", "00100", "00000"),
    }

    def __init__(self, root: tk.Tk) -> None:
        self.root = root
        self.root.title("MiniJV880 External Remote GUI prototype")

        config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
        self.settings_path = config_home / "minijv880_remote_gui" / "settings.json"
        self._loaded_window_geometry: str | None = None
        self._loaded_lcd_color_preset: str | None = None
        self._loaded_lcd_text_color_preset: str | None = None
        self._loaded_lcd_background_tone: str | None = None
        self._loaded_lcd_invert_colors: bool | None = None
        self._loaded_lcd_render_mode: str | None = None
        self._loaded_serial_details_visible = False
        self._loaded_safety_notes_visible = False

        self.base_url = tk.StringVar(value=DEFAULT_BASE_URL)
        self.timeout_s = tk.DoubleVar(value=2.0)
        self.status = tk.StringVar(value="Ready")
        self.lcd1 = tk.StringVar(value="")
        self.lcd2 = tk.StringVar(value="")
        self.auto_lcd = tk.BooleanVar(value=True)
        self.serial_port = tk.StringVar(value="/dev/ttyUSB0")
        self.serial_baud = tk.IntVar(value=38400)
        self.serial_status = tk.StringVar(value="Serial stopped")
        self.serial_running = False
        self.serial_open = False
        self.serial_thread: threading.Thread | None = None
        self.serial_stdout_mirror = tk.BooleanVar(value=False)

        runtime_dir = Path(
            os.environ.get(
                "XDG_RUNTIME_DIR",
                f"/run/user/{os.getuid()}",
            )
        )
        self.minicom_bridge_link = runtime_dir / "minijv880-log"
        self.minicom_bridge_master_fd: int | None = None
        self.minicom_bridge_connected = False
        self.minicom_bridge_lock = threading.Lock()
        self.minicom_bridge_monitor_thread: threading.Thread | None = None
        self.minicom_bridge_process: subprocess.Popen | None = None
        self.minicom_button: tk.Widget | None = None
        self.minicom_button_text_item: int | None = None

        self._pending_serial_lcd1: str | None = None
        self.connection_window: tk.Toplevel | None = None
        self.display_settings_window: tk.Toplevel | None = None
        self.serial_window: tk.Toplevel | None = None
        self.serial_details_window: tk.Toplevel | None = None
        self.serial_log_text: tk.Text | None = None
        self.serial_details_visible = False
        self.serial_details_frame: ttk.Frame | None = None
        self.serial_details_button: tk.Widget | None = None
        self.serial_details_text_item: int | None = None
        self.serial_toggle_button: tk.Widget | None = None
        self.serial_toggle_text_item: int | None = None
        self.serial_toggle_instances: list[tuple[tk.Canvas, int, int]] = []
        self._enter_long_after_id: str | None = None
        self.safety_notes_window: tk.Toplevel | None = None
        self.safety_notes_visible = False
        self.safety_notes_frame: ttk.LabelFrame | None = None
        self.safety_notes_button: tk.Widget | None = None
        self.safety_notes_text_item: int | None = None

        self.remote_held = {
            "DATA": False,
            "ENTER": False,
            "TONESEL": False,
        }
        self.held_status = tk.StringVar(value="")
        self.led_widgets: dict[str, tuple[tk.Canvas, int]] = {}
        self.led_states: dict[str, str] = {}
        self._led_blink_on = True
        self._led_blink_after_id: str | None = None
        self._lcd_after_id: str | None = None

        self.lcd_outer_frame: tk.Frame | None = None
        self.lcd_bezel_frame: tk.Frame | None = None
        self.lcd_canvas: tk.Canvas | None = None
        self.lcd_text_items: list[int] = []
        self.lcd_char_items: list[list[int]] = []
        self.lcd_dot_items: list[list[list[int]]] = []
        self.lcd_cursor_item: int | None = None
        self.lcd_cursor_dot_items: list[int] = []
        self.lcd_hd44780_font_family = "5x8 LCD HD44780U A02"
        self.lcd_hd44780_font_size = 12
        self.lcd_fallback_font = ("TkFixedFont", 15, "bold")
        self.lcd_font = self.lcd_fallback_font
        self.lcd_font_obj = None
        self.lcd_render_mode_presets = {
            "dotmatrix": "Dot matrix 5x7",
            "hd44780": "HD44780 font, if available",
            "tk_fixed": "Tk fixed fallback",
        }
        self.lcd_render_mode = tk.StringVar(value="dotmatrix")
        self.lcd_text_color_presets = {
            "auto": ("Auto / recommended", None),
            "black": ("Black", "#000000"),
            "white": ("White", "#f2f6ff"),
            "dark_blue": ("Blue", "#0058cc"),
        }
        self.lcd_text_color_preset = tk.StringVar(value="auto")
        self.lcd_background_tone_presets = {
            "darker": ("Darker", -0.30),
            "slightly_darker": ("Slightly darker", -0.15),
            "normal": ("Normal", 0.0),
            "brighter": ("Brighter", 0.18),
            "very_bright": ("Very bright", 0.35),
        }
        self.lcd_background_tone = tk.StringVar(value="normal")
        self.lcd_invert_colors = tk.BooleanVar(value=False)
        self.lcd_dot_cols = 5
        self.lcd_dot_rows = 7
        self.lcd_dot_size = 3
        self.lcd_dot_height_extra = 0
        self.lcd_dot_gap = 1
        self.lcd_char_gap_px = 2
        self.lcd_dot_left_offset = 1
        self.lcd_dot_top_offset = 2
        self.lcd_row2_dot_offset = 1
        self.lcd_char_width = 14
        self.lcd_cell_extra_px = 1
        self.lcd_row_extra_px = 8
        self.lcd_min_row_height = 35
        self.lcd_cursor_height = 2
        self.lcd_cursor_bottom_margin = 1
        self.lcd_row_height = 24
        self.lcd_pad_x = 6
        self.lcd_pad_y = 6
        self.lcd_cursor_row = 0
        self.lcd_cursor_col = 0
        self.lcd_cursor_enabled = False
        self.lcd_cursor_visible = False
        self.lcd_cursor_blink_on = True
        self.lcd_cursor_blink_ms = 400
        self._lcd_cursor_blink_after_id: str | None = None
        self.lcd_cursor_address = 0

        self.key_bg = "#cfcfcf"
        self.key_active_bg = "#c2c2c2"
        self.key_outline = "#777777"
        self.key_border_width = 2
        self.key_corner_radius = 10
        self.key_highlight = "#eeeeee"
        self.key_drop_shadow = "#a5a5a5"

        self.lcd_color_presets = {
            # key: (label, lcd_bg, recommended_lcd_fg, outer_bg, bezel_bg)
            "yellow_green": ("Yellow green", "#e2e324", "#000000", "#9a967c", "#b8b073"),
            "classic_green": ("Classic green", "#9fdd5a", "#000000", "#5f774c", "#7fa85b"),
            "amber": ("Amber", "#f0b642", "#000000", "#806540", "#b9853d"),
            "red_lcd": ("Red LCD", "#d9554a", "#1a0000", "#70403d", "#ad5148"),
            "blue_lcd": ("Blue LCD", "#7fb3ff", "#f2f6ff", "#526678", "#6f8caf"),
            "gray_contrast": ("Gray contrast", "#d6d6c8", "#000000", "#85857a", "#adad9c"),
        }
        self.lcd_color_preset = tk.StringVar(value="yellow_green")

        self.lcd_outer_bg = "#9a967c"
        self.lcd_bezel_bg = "#b8b073"
        self.lcd_bg = "#e2e324"
        self.lcd_fg = "#000000"

        self.lcd1.trace_add("write", lambda *_args: self._update_lcd_canvas())
        self.lcd2.trace_add("write", lambda *_args: self._update_lcd_canvas())

        self._load_settings()

        self._configure_styles()
        self._build_ui()
        self._start_lcd_cursor_blink_timer()
        self._apply_loaded_settings()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

        # Populate LCD1/LCD2 once at startup. After this, the preferred
        # automatic readback path is the passive serial LCD monitor.
        self.root.after(500, self.refresh_lcd)

    def _load_settings(self) -> None:
        try:
            with self.settings_path.open("r", encoding="utf-8") as f:
                data = json.load(f)
        except FileNotFoundError:
            return
        except Exception:
            return

        if not isinstance(data, dict):
            return

        value = data.get("base_url")
        if isinstance(value, str) and value.strip():
            self.base_url.set(value.strip())

        value = data.get("timeout_s")
        try:
            if value is not None:
                self.timeout_s.set(float(value))
        except Exception:
            pass

        value = data.get("serial_port")
        if isinstance(value, str) and value.strip():
            self.serial_port.set(value.strip())

        value = data.get("serial_baud")
        try:
            if value is not None:
                self.serial_baud.set(int(value))
        except Exception:
            pass

        value = data.get("auto_lcd")
        if isinstance(value, bool):
            self.auto_lcd.set(value)

        value = data.get("window_geometry")
        if isinstance(value, str) and "x" in value:
            self._loaded_window_geometry = value

        value = data.get("lcd_color_preset")
        if isinstance(value, str) and value in self.lcd_color_presets:
            self._loaded_lcd_color_preset = value
            self.lcd_color_preset.set(value)

        value = data.get("lcd_text_color_preset")
        if isinstance(value, str) and value in self.lcd_text_color_presets:
            self._loaded_lcd_text_color_preset = value
            self.lcd_text_color_preset.set(value)

        value = data.get("lcd_background_tone")
        if isinstance(value, str) and value in self.lcd_background_tone_presets:
            self._loaded_lcd_background_tone = value
            self.lcd_background_tone.set(value)

        value = data.get("lcd_invert_colors")
        if isinstance(value, bool):
            self._loaded_lcd_invert_colors = value
            self.lcd_invert_colors.set(value)

        value = data.get("lcd_render_mode")
        if isinstance(value, str) and value in self.lcd_render_mode_presets:
            self._loaded_lcd_render_mode = value
            self.lcd_render_mode.set(value)

        self._loaded_serial_details_visible = bool(data.get("serial_details_visible", False))
        self._loaded_safety_notes_visible = bool(data.get("safety_notes_visible", False))

    def _save_settings(self) -> None:
        data = {
            "base_url": self.base_url.get().strip(),
            "timeout_s": float(self.timeout_s.get()),
            "serial_port": self.serial_port.get().strip(),
            "serial_baud": int(self.serial_baud.get()),
            "auto_lcd": bool(self.auto_lcd.get()),
            "lcd_color_preset": self.lcd_color_preset.get(),
            "lcd_text_color_preset": self.lcd_text_color_preset.get(),
            "lcd_background_tone": self.lcd_background_tone.get(),
            "lcd_invert_colors": bool(self.lcd_invert_colors.get()),
            "lcd_render_mode": self.lcd_render_mode.get(),
            "window_geometry": self.root.geometry(),
            "serial_details_visible": bool(self.serial_details_visible),
            "safety_notes_visible": bool(self.safety_notes_visible),
        }

        try:
            self.settings_path.parent.mkdir(parents=True, exist_ok=True)
            tmp = self.settings_path.with_suffix(".json.tmp")
            with tmp.open("w", encoding="utf-8") as f:
                json.dump(data, f, indent=2, sort_keys=True)
                f.write("\n")
            tmp.replace(self.settings_path)
        except Exception as exc:
            try:
                self.status.set(f"Settings save failed: {exc}")
            except Exception:
                pass

    def _apply_loaded_settings(self) -> None:
        if self._loaded_window_geometry:
            try:
                self.root.geometry(self._loaded_window_geometry)
            except Exception:
                pass

        if self._loaded_lcd_color_preset:
            self.apply_lcd_color_preset(self._loaded_lcd_color_preset)

        if self._loaded_lcd_text_color_preset:
            self.apply_lcd_text_color_preset(self._loaded_lcd_text_color_preset)

        if self._loaded_lcd_background_tone:
            self.apply_lcd_background_tone(self._loaded_lcd_background_tone)

        if self._loaded_lcd_invert_colors is not None:
            self.apply_lcd_invert_colors(self._loaded_lcd_invert_colors)

        if self._loaded_lcd_render_mode:
            self.apply_lcd_render_mode(self._loaded_lcd_render_mode)

        if self._loaded_serial_details_visible:
            self.toggle_serial_details()

        if self._loaded_safety_notes_visible:
            self.toggle_safety_notes()

    def _on_close(self) -> None:
        self.serial_running = False
        self._close_minicom_bridge()

        if self._lcd_cursor_blink_after_id is not None:
            try:
                self.root.after_cancel(self._lcd_cursor_blink_after_id)
            except Exception:
                pass
            self._lcd_cursor_blink_after_id = None

        self._save_settings()
        self.root.destroy()

    def _configure_styles(self) -> None:
        style = ttk.Style(self.root)

        # Keep the GUI compact: the remote keyboard has many controls, and
        # large default paddings make the window unnecessarily tall.
        style.configure("TButton", padding=(4, 2))
        style.configure("TCheckbutton", padding=(2, 1))
        style.configure("TEntry", padding=(2, 1))
        style.configure("TLabelframe", padding=(4, 3))
        style.configure("TLabelframe.Label", padding=(2, 0))
        style.configure("TNotebook.Tab", padding=(8, 3))

    def _key_button(
        self,
        parent,
        text: str,
        command,
        row: int,
        column: int,
        *,
        width: int = 14,
        height: int = 2,
        columnspan: int = 1,
        padx: int = 4,
        pady: int = 4,
    ) -> tk.Canvas:
        # Canvas-based key used for the MiniJV880 current hardware tab.
        # It keeps the existing _key_button() call sites unchanged while
        # matching the rounded 3D visual style of the JV-880 panel buttons.
        width_px = max(88, width * 8 + 22)
        height_px = max(44, height * 15 + 18)

        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.root.cget("background"),
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
        button_rect = self._create_rounded_button_rect(
            canvas,
            margin,
            margin,
            width_px - margin,
            height_px - margin,
        )

        canvas.create_text(
            width_px // 2,
            height_px // 2,
            text=text,
            justify="center",
            anchor="center",
            fill="black",
            font=("TkDefaultFont", 9),
        )

        def press(_event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_active_bg)

        def release(event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_bg)
            if event is None:
                command()
                return
            x = event.x
            y = event.y
            if 0 <= x <= width_px and 0 <= y <= height_px:
                command()

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")

        return canvas

    def _lcd_display(
        self,
        parent,
        variable: tk.StringVar,
        row: int,
        column: int,
        *,
        columnspan: int = 1,
    ) -> tk.Frame:
        # Hardware-like 24-character LCD readback display.
        outer = tk.Frame(
            parent,
            bg=self.lcd_outer_bg,
            bd=1,
            relief="raised",
            highlightthickness=0,
        )
        outer.grid(row=row, column=column, columnspan=columnspan, sticky="ew", padx=0, pady=2)

        bezel = tk.Frame(
            outer,
            bg=self.lcd_bezel_bg,
            bd=1,
            relief="sunken",
            highlightthickness=0,
        )
        bezel.pack(fill="both", expand=True, padx=2, pady=2)

        label = tk.Label(
            bezel,
            textvariable=variable,
            font=("TkFixedFont", 15, "bold"),
            width=24,
            anchor="w",
            justify="left",
            bg=self.lcd_bg,
            fg=self.lcd_fg,
            relief="flat",
            borderwidth=0,
            padx=8,
            pady=4,
            takefocus=False,
        )
        label.pack(fill="both", expand=True, padx=2, pady=2)

        return outer

    def _lcd_dual_display(
        self,
        parent,
        row: int,
        column: int,
        *,
        columnspan: int = 1,
    ) -> tk.Frame:
        # Hardware-like single 2-line LCD readback display.
        # LCD1/LCD2 remain StringVars, but the rendering is Canvas-based so
        # a firmware-read cursor can be drawn without modifying the text.
        outer = tk.Frame(
            parent,
            bg=self.lcd_outer_bg,
            bd=1,
            relief="raised",
            highlightthickness=0,
        )
        outer.grid(row=row, column=column, columnspan=columnspan, sticky="", padx=0, pady=2)
        self.lcd_outer_frame = outer

        bezel = tk.Frame(
            outer,
            bg=self.lcd_bezel_bg,
            bd=1,
            relief="sunken",
            highlightthickness=0,
        )
        bezel.pack(fill="both", expand=True, padx=2, pady=2)
        self.lcd_bezel_frame = bezel

        self.lcd_font_obj = None

        dot_w = int(self.lcd_dot_size)
        dot_h = dot_w + int(self.lcd_dot_height_extra)
        dot_pitch_x = dot_w + int(self.lcd_dot_gap)
        dot_pitch_y = dot_h + int(self.lcd_dot_gap)
        dot_matrix_width = int(self.lcd_dot_cols) * dot_pitch_x - int(self.lcd_dot_gap)
        dot_matrix_height = int(self.lcd_dot_rows) * dot_pitch_y - int(self.lcd_dot_gap)

        self.lcd_char_width = dot_matrix_width + int(self.lcd_char_gap_px)
        self.lcd_row_height = max(
            int(self.lcd_min_row_height),
            dot_matrix_height + int(self.lcd_row_extra_px),
        )

        print(
            "MiniJV880 remote GUI LCD renderer: dotmatrix",
            "dot_size:",
            self.lcd_dot_size,
            "char_width:",
            self.lcd_char_width,
            "row_height:",
            self.lcd_row_height,
            flush=True,
        )

        row2_pixel_offset = int(self.lcd_row2_dot_offset) * dot_pitch_y

        width_px = self.lcd_pad_x * 2 + self.lcd_char_width * 24
        height_px = self.lcd_pad_y * 2 + self.lcd_row_height * 2 + row2_pixel_offset

        canvas = tk.Canvas(
            bezel,
            width=width_px,
            height=height_px,
            bg=self.lcd_bg,
            bd=0,
            relief="flat",
            highlightthickness=0,
            takefocus=False,
        )
        canvas.pack(fill="both", expand=True, padx=2, pady=2)

        self.lcd_canvas = canvas
        self.lcd_text_items = []
        self.lcd_char_items = []
        self.lcd_dot_items = []

        dot_w = int(self.lcd_dot_size)
        dot_h = dot_w + int(self.lcd_dot_height_extra)
        dot_pitch_x = dot_w + int(self.lcd_dot_gap)
        dot_pitch_y = dot_h + int(self.lcd_dot_gap)

        for display_row in range(2):
            row_cells: list[list[int]] = []
            row_base = self.lcd_pad_y + display_row * self.lcd_row_height
            for col in range(24):
                cell_items: list[int] = []
                x0 = self.lcd_pad_x + col * self.lcd_char_width + int(self.lcd_dot_left_offset)
                row_dot_offset = int(self.lcd_row2_dot_offset) if display_row == 1 else 0
                y0 = row_base + int(self.lcd_dot_top_offset) + row_dot_offset * dot_pitch_y

                for dot_row in range(int(self.lcd_dot_rows)):
                    for dot_col in range(int(self.lcd_dot_cols)):
                        x1 = x0 + dot_col * dot_pitch_x
                        y1 = y0 + dot_row * dot_pitch_y
                        item = canvas.create_rectangle(
                            x1,
                            y1,
                            x1 + dot_w - 1,
                            y1 + dot_h - 1,
                            fill=self.lcd_fg,
                            outline=self.lcd_fg,
                            state="hidden",
                        )
                        cell_items.append(item)

                row_cells.append(cell_items)
            self.lcd_dot_items.append(row_cells)

        text_font = self._lcd_text_font_for_mode()
        for display_row in range(2):
            row_items: list[int] = []
            row_base = self.lcd_pad_y + display_row * self.lcd_row_height
            row_pixel_offset = int(self.lcd_row2_dot_offset) * dot_pitch_y if display_row == 1 else 0
            y = row_base + row_pixel_offset + self.lcd_row_height // 2
            for col in range(24):
                x = self.lcd_pad_x + col * self.lcd_char_width + self.lcd_char_width // 2
                item = canvas.create_text(
                    x,
                    y,
                    text=" ",
                    fill=self.lcd_fg,
                    font=text_font,
                    anchor="center",
                    state="hidden",
                )
                row_items.append(item)
            self.lcd_char_items.append(row_items)

        self.lcd_cursor_item = None
        self.lcd_cursor_dot_items = []
        for _dot_col in range(int(self.lcd_dot_cols)):
            item = canvas.create_rectangle(
                0,
                0,
                0,
                0,
                fill=self.lcd_fg,
                outline=self.lcd_fg,
                state="hidden",
            )
            self.lcd_cursor_dot_items.append(item)

        self._update_lcd_canvas()
        return outer

    def _lcd_text_font_for_mode(self):
        mode = self.lcd_render_mode.get().strip()

        if mode == "hd44780":
            return self._select_lcd_font()

        if mode == "tk_fixed":
            return self.lcd_fallback_font

        return self.lcd_fallback_font

    def _select_lcd_font(self):
        try:
            families = set(tkfont.families())
            if self.lcd_hd44780_font_family in families:
                return (self.lcd_hd44780_font_family, self.lcd_hd44780_font_size, "normal")
        except Exception:
            pass

        return self.lcd_fallback_font

    def _lcd_display_line(self, value: str) -> str:
        value = (value or "")[:24]
        return value + (" " * max(0, 24 - len(value)))

    def _start_lcd_cursor_blink_timer(self) -> None:
        if self._lcd_cursor_blink_after_id is not None:
            return

        self._lcd_cursor_blink_after_id = self.root.after(
            int(self.lcd_cursor_blink_ms),
            self._blink_lcd_cursor,
        )

    def _blink_lcd_cursor(self) -> None:
        self._lcd_cursor_blink_after_id = None
        self.lcd_cursor_blink_on = not self.lcd_cursor_blink_on
        self._update_lcd_canvas()
        self._start_lcd_cursor_blink_timer()

    def _lcd_glyph_rows_for_char(self, ch: str) -> tuple[str, ...]:
        # MiniJV880 firmware currently maps the real LCD arrows to '<' and '>'.
        # Draw them as arrows in the dot renderer rather than as mathematical
        # less-than / greater-than signs.
        glyph = self.LCD_DOT_GLYPHS.get(ch)
        if glyph is None:
            glyph = self.LCD_DOT_GLYPHS.get("?", self.LCD_DOT_GLYPHS[" "])
        return glyph

    def _draw_lcd_dot_char(self, canvas: tk.Canvas, items: list[int], ch: str) -> None:
        glyph = self._lcd_glyph_rows_for_char(ch)
        expected = int(self.lcd_dot_cols) * int(self.lcd_dot_rows)
        if len(items) < expected:
            return

        effective_fg = self._lcd_effective_colors()[1]

        idx = 0
        for dot_row in range(int(self.lcd_dot_rows)):
            row_bits = glyph[dot_row] if dot_row < len(glyph) else ""
            for dot_col in range(int(self.lcd_dot_cols)):
                item = items[idx]
                bit = row_bits[dot_col] if dot_col < len(row_bits) else " "
                active = bit not in {" ", ".", "0"}
                canvas.itemconfigure(
                    item,
                    fill=effective_fg,
                    outline=effective_fg,
                    state="normal" if active else "hidden",
                )
                idx += 1


    def _update_lcd_canvas(self) -> None:
        canvas = self.lcd_canvas
        if canvas is None:
            return

        lines = (
            self._lcd_display_line(self.lcd1.get()),
            self._lcd_display_line(self.lcd2.get()),
        )

        effective_bg, effective_fg = self._lcd_effective_colors()
        try:
            canvas.configure(bg=effective_bg)
        except Exception:
            pass

        render_mode = self.lcd_render_mode.get().strip()

        if render_mode == "dotmatrix" and self.lcd_dot_items:
            if self.lcd_char_items:
                for row_items in self.lcd_char_items:
                    for item in row_items:
                        canvas.itemconfigure(item, state="hidden")

            for row, text in enumerate(lines):
                if row >= len(self.lcd_dot_items):
                    continue
                row_cells = self.lcd_dot_items[row]
                for col in range(min(24, len(row_cells))):
                    self._draw_lcd_dot_char(canvas, row_cells[col], text[col])

        elif self.lcd_char_items:
            if self.lcd_dot_items:
                for row_cells in self.lcd_dot_items:
                    for cell_items in row_cells:
                        for item in cell_items:
                            canvas.itemconfigure(item, state="hidden")

            text_font = self._lcd_text_font_for_mode()
            for row, text in enumerate(lines):
                if row >= len(self.lcd_char_items):
                    continue
                row_items = self.lcd_char_items[row]
                for col in range(min(24, len(row_items))):
                    canvas.itemconfigure(
                        row_items[col],
                        text=text[col],
                        fill=effective_fg,
                        font=text_font,
                        state="normal",
                    )

        else:
            # Fallback for older in-memory instances, not normally used after
            # rebuilding the widget.
            for idx, text in enumerate(lines):
                if idx < len(self.lcd_text_items):
                    canvas.itemconfigure(self.lcd_text_items[idx], text=text, fill=effective_fg)

        if not self.lcd_cursor_dot_items:
            return

        row = int(self.lcd_cursor_row)
        col = int(self.lcd_cursor_col)
        cursor_active = bool(self.lcd_cursor_enabled or self.lcd_cursor_visible)
        if not (cursor_active and self.lcd_cursor_blink_on and 0 <= row < 2 and 0 <= col < 24):
            for item in self.lcd_cursor_dot_items:
                canvas.itemconfigure(item, state="hidden")
            return

        dot_w = int(self.lcd_dot_size)
        dot_h = dot_w + int(self.lcd_dot_height_extra)
        dot_gap = int(self.lcd_dot_gap)
        dot_pitch_x = dot_w + dot_gap
        dot_pitch_y = dot_h + dot_gap
        row_pixel_offset = int(self.lcd_row2_dot_offset) * dot_pitch_y if row == 1 else 0

        x0 = self.lcd_pad_x + col * self.lcd_char_width + int(self.lcd_dot_left_offset)
        y_base = self.lcd_pad_y + row * self.lcd_row_height + row_pixel_offset
        y0 = y_base + self.lcd_row_height - int(self.lcd_cursor_bottom_margin) - dot_h

        for dot_col, item in enumerate(self.lcd_cursor_dot_items):
            x1 = x0 + dot_col * dot_pitch_x
            y1 = y0
            canvas.coords(item, x1, y1, x1 + dot_w - 1, y1 + dot_h - 1)
            canvas.itemconfigure(
                item,
                fill=effective_fg,
                outline=effective_fg,
                state="normal",
            )
            canvas.tag_raise(item)


    def _rounded_rect_points(self, x1: int, y1: int, x2: int, y2: int, radius: int) -> list[int]:
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

    def _create_rounded_button_rect(
        self,
        canvas: tk.Canvas,
        x1: int,
        y1: int,
        x2: int,
        y2: int,
    ) -> int:
        # Canvas rounded rectangle used by the large panel buttons.
        # The returned item is still the main face, so the existing
        # press/release fill changes continue to work unchanged.
        radius = min(
            self.key_corner_radius,
            max(1, (x2 - x1) // 2),
            max(1, (y2 - y1) // 2),
        )

        # Subtle drop shadow: down/right only, behind the button face.
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

        # Symmetric inner bevel: one continuous rounded inset outline.
        # This keeps the 3D look without segmented corner artifacts.
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

    def _led_colors_for_state(self, state: str) -> tuple[str, str]:
        normalized = state.strip().upper()

        if normalized == "ON":
            return "#ff3030", "#8a0000"
        if normalized in {"RED", "RED_ON", "ON_RED"}:
            return "#ff3030", "#8a0000"
        if normalized in {"GREEN", "GREEN_ON", "ON_GREEN"}:
            return "#00c853", "#006b2e"
        if normalized == "UNKNOWN":
            return "#777777", "#555555"

        return "#3a2630", "#2a1a22"

    def _apply_led_visual(self, led_id: str) -> None:
        # SERIAL_ACTIVE can have multiple visual instances: one in the compact
        # topbar and one in the optional Serial window. Do not use the legacy
        # single-widget led_widgets entry for it, because that entry can point
        # to a Canvas destroyed when the Serial window is closed.
        if led_id == "SERIAL_ACTIVE":
            self._apply_serial_toggle_visual()
            return

        led = self.led_widgets.get(led_id)
        if not led:
            return

        canvas, led_item = led
        normalized = self.led_states.get(led_id, "OFF").strip().upper()

        if normalized == "BLINK":
            visual_state = "ON" if self._led_blink_on else "OFF"
        else:
            visual_state = normalized

        fill, outline = self._led_colors_for_state(visual_state)

        try:
            if not canvas.winfo_exists():
                self.led_widgets.pop(led_id, None)
                return
            canvas.itemconfigure(led_item, fill=fill, outline=outline)
        except Exception:
            self.led_widgets.pop(led_id, None)


    def _has_blinking_leds(self) -> bool:
        return any(state == "BLINK" for state in self.led_states.values())

    def _start_led_blink_timer_if_needed(self) -> None:
        if self._has_blinking_leds():
            if self._led_blink_after_id is None:
                self._led_blink_after_id = self.root.after(450, self._blink_leds)
            return

        self._led_blink_on = True

        if self._led_blink_after_id is not None:
            try:
                self.root.after_cancel(self._led_blink_after_id)
            except Exception:
                pass
            self._led_blink_after_id = None

    def _blink_leds(self) -> None:
        self._led_blink_after_id = None

        if not self._has_blinking_leds():
            self._led_blink_on = True
            return

        self._led_blink_on = not self._led_blink_on

        for led_id, state in list(self.led_states.items()):
            if state == "BLINK":
                self._apply_led_visual(led_id)

        self._led_blink_after_id = self.root.after(450, self._blink_leds)

    def _set_led_state(self, led_id: str, state: str) -> None:
        normalized = state.strip().upper()

        if normalized not in {"ON", "OFF", "BLINK", "UNKNOWN", "RED", "RED_ON", "ON_RED", "GREEN", "GREEN_ON", "ON_GREEN"}:
            normalized = "OFF"

        self.led_states[led_id] = normalized
        self._apply_led_visual(led_id)
        self._start_led_blink_timer_if_needed()

    def _apply_serial_toggle_visual(self) -> None:
        normalized = self.led_states.get("SERIAL_ACTIVE", "OFF").strip().upper()
        if normalized == "BLINK":
            visual_state = "ON" if self._led_blink_on else "OFF"
        else:
            visual_state = normalized

        fill, outline = self._led_colors_for_state(visual_state)
        label = "Stop serial" if self.serial_running else "Start serial"

        live_instances: list[tuple[tk.Canvas, int, int]] = []
        for canvas, led_item, text_item in self.serial_toggle_instances:
            try:
                if not canvas.winfo_exists():
                    continue
                canvas.itemconfigure(led_item, fill=fill, outline=outline)
                canvas.itemconfigure(text_item, text=label)
                live_instances.append((canvas, led_item, text_item))
            except Exception:
                pass

        self.serial_toggle_instances = live_instances

    def _refresh_local_leds(self) -> None:
        self._set_led_state(
            "TONESEL_HOLD",
            "ON" if self.remote_held.get("TONESEL", False) else "OFF",
        )
        self._set_led_state(
            "DATA_HOLD",
            "ON" if self.remote_held.get("DATA", False) else "OFF",
        )

    def _led_key_button(
        self,
        parent,
        led_id: str,
        text: str,
        command,
        row: int,
        column: int,
        *,
        width_px: int = 136,
        height_px: int = 68,
        columnspan: int = 1,
        padx: int = 4,
        pady: int = 4,
    ) -> tk.Canvas:
        # Canvas-based button used only where an integrated JV-880-style LED
        # is needed. The LED is drawn inside the key, near the top center.
        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.root.cget("background"),
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
        button_rect = self._create_rounded_button_rect(
            canvas,
            margin,
            margin,
            width_px - margin,
            height_px - margin,
        )

        led_w = 28
        led_h = 5
        led_x0 = (width_px - led_w) // 2
        led_y0 = 9
        led_item = canvas.create_rectangle(
            led_x0,
            led_y0,
            led_x0 + led_w,
            led_y0 + led_h,
            fill="#3a2630",
            outline="#2a1a22",
        )

        canvas.create_text(
            width_px // 2,
            height_px // 2 + 8,
            text=text,
            justify="center",
            anchor="center",
            fill="black",
            font=("TkDefaultFont", 9),
        )

        def press(_event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_active_bg)

        def release(event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_bg)
            if event is None:
                command()
                return
            x = event.x
            y = event.y
            if 0 <= x <= width_px and 0 <= y <= height_px:
                command()

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")

        self.led_widgets[led_id] = (canvas, led_item)

        if led_id not in self.led_states:
            self.led_states[led_id] = "OFF"

        self._apply_led_visual(led_id)
        return canvas

    def _plain_panel_button(
        self,
        parent,
        text: str,
        command,
        row: int,
        column: int,
        *,
        width_px: int = 194,
        height_px: int = 82,
        columnspan: int = 1,
        padx: int = 4,
        pady: int = 4,
    ) -> tk.Canvas:
        # Canvas-based key without LED, used to keep the Original JV-880
        # panel geometrically consistent with LED keys.
        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.root.cget("background"),
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
        button_rect = self._create_rounded_button_rect(
            canvas,
            margin,
            margin,
            width_px - margin,
            height_px - margin,
        )

        canvas.create_text(
            width_px // 2,
            height_px // 2,
            text=text,
            justify="center",
            anchor="center",
            fill="black",
            font=("TkDefaultFont", 9),
        )

        def press(_event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_active_bg)

        def release(event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_bg)
            if event is None:
                command()
                return
            x = event.x
            y = event.y
            if 0 <= x <= width_px and 0 <= y <= height_px:
                command()

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")
        return canvas

    def _preview_hold_button(
        self,
        parent,
        text: str,
        row: int,
        column: int,
        *,
        width_px: int,
        height_px: int,
        columnspan: int = 1,
        padx: int = 4,
        pady: int = 4,
    ) -> tk.Canvas:
        # Existing PREVIEW key with physical-like momentary behavior:
        # mouse button down -> PREVIEW down
        # mouse button up   -> PREVIEW up
        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.root.cget("background"),
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
        button_rect = self._create_rounded_button_rect(
            canvas,
            margin,
            margin,
            width_px - margin,
            height_px - margin,
        )

        canvas.create_text(
            width_px // 2,
            height_px // 2,
            text=text,
            justify="center",
            anchor="center",
            fill="black",
            font=("TkDefaultFont", 9),
        )

        pressed = {"active": False}

        def press(_event=None) -> None:
            if pressed["active"]:
                return
            pressed["active"] = True
            canvas.itemconfigure(button_rect, fill=self.key_active_bg)
            self.down("PREVIEW")

        def release(_event=None) -> None:
            if not pressed["active"]:
                return
            pressed["active"] = False
            canvas.itemconfigure(button_rect, fill=self.key_bg)
            self.up("PREVIEW")

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")
        return canvas

    def _top_action_button(
        self,
        parent,
        text: str,
        command,
        row: int,
        column: int,
        *,
        width_px: int = 104,
        height_px: int = 30,
        padx=0,
        pady=0,
        sticky: str = "w",
    ) -> tuple[tk.Canvas, int]:
        # Compact rounded 3D Canvas button for the top control panel.
        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.root.cget("background"),
            takefocus=False,
        )
        canvas.grid(row=row, column=column, padx=padx, pady=pady, sticky=sticky)

        margin = 3
        button_rect = self._create_rounded_button_rect(
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
            font=("TkDefaultFont", 9),
        )

        def press(_event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_active_bg)

        def release(event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_bg)
            if event is None:
                command()
                return
            x = event.x
            y = event.y
            if 0 <= x <= width_px and 0 <= y <= height_px:
                command()

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")
        return canvas, text_item

    def _serial_toggle_led_button(
        self,
        parent,
        row: int,
        column: int,
        *,
        width_px: int = 118,
        height_px: int = 30,
        padx: int = 0,
        pady: int = 0,
    ) -> tk.Canvas:
        # Compact serial monitor button with the SERIAL_ACTIVE LED embedded
        # inside the key. This replaces the separate small status LED next to
        # the Start/Stop serial button.
        canvas = tk.Canvas(
            parent,
            width=width_px,
            height=height_px,
            highlightthickness=0,
            borderwidth=0,
            bg=self.root.cget("background"),
            takefocus=False,
        )
        canvas.grid(row=row, column=column, padx=padx, pady=pady, sticky="w")

        margin = 3
        button_rect = self._create_rounded_button_rect(
            canvas,
            margin,
            margin,
            width_px - margin,
            height_px - margin,
        )

        led_item = canvas.create_rectangle(
            11,
            9,
            23,
            21,
            fill="#3a2630",
            outline="#2a1a22",
        )

        text_item = canvas.create_text(
            74,
            height_px // 2,
            text="Start serial",
            justify="center",
            anchor="center",
            fill="black",
            font=("TkDefaultFont", 9),
        )

        def press(_event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_active_bg)

        def release(event=None) -> None:
            canvas.itemconfigure(button_rect, fill=self.key_bg)
            if event is None:
                self.toggle_serial_monitor()
                return
            x = event.x
            y = event.y
            if 0 <= x <= width_px and 0 <= y <= height_px:
                self.toggle_serial_monitor()

        canvas.bind("<ButtonPress-1>", press)
        canvas.bind("<ButtonRelease-1>", release)
        canvas.configure(cursor="hand2")

        self.serial_toggle_button = canvas
        self.serial_toggle_text_item = text_item
        self.serial_toggle_instances.append((canvas, led_item, text_item))
        self.led_widgets["SERIAL_ACTIVE"] = (canvas, led_item)

        if "SERIAL_ACTIVE" not in self.led_states:
            self.led_states["SERIAL_ACTIVE"] = "OFF"

        self._apply_led_visual("SERIAL_ACTIVE")
        return canvas

    def _small_status_led(
        self,
        parent,
        led_id: str,
        row: int,
        column: int,
        *,
        padx: int = 3,
        pady: int = 2,
    ) -> tk.Canvas:
        canvas = tk.Canvas(
            parent,
            width=18,
            height=18,
            highlightthickness=0,
            borderwidth=0,
            bg=self.root.cget("background"),
            takefocus=False,
        )
        canvas.grid(row=row, column=column, padx=padx, pady=pady, sticky="w")

        led_item = canvas.create_oval(
            3,
            3,
            15,
            15,
            fill="#3a2630",
            outline="#2a1a22",
        )

        self.led_widgets[led_id] = (canvas, led_item)

        if led_id not in self.led_states:
            self.led_states[led_id] = "OFF"

        self._apply_led_visual(led_id)
        return canvas

    # ------------------------------------------------------------------
    # UI
    # ------------------------------------------------------------------
    def _build_ui(self) -> None:
        root = self.root
        root.columnconfigure(0, weight=1)

        top = ttk.LabelFrame(root, text="Remote display / status")
        top.grid(row=0, column=0, sticky="ew", padx=10, pady=6)

        top.columnconfigure(0, weight=1)

        toolbar = ttk.Frame(top)
        toolbar.grid(row=0, column=0, sticky="ew", padx=6, pady=(6, 2))
        toolbar.columnconfigure(2, weight=1)
        toolbar.columnconfigure(5, weight=1)

        self._top_action_button(
            toolbar,
            "Connection...",
            self.open_connection_window,
            0,
            0,
            width_px=112,
            padx=(0, 6),
            pady=0,
            sticky="w",
        )

        self._top_action_button(
            toolbar,
            "Display...",
            self.open_display_settings_window,
            0,
            1,
            width_px=104,
            padx=(0, 8),
            pady=0,
            sticky="w",
        )

        ttk.Label(
            toolbar,
            textvariable=self.status,
            width=22,
            anchor="w",
        ).grid(row=0, column=2, sticky="ew", padx=(0, 10))

        self._serial_toggle_led_button(toolbar, 0, 3)

        self._top_action_button(
            toolbar,
            "Serial...",
            self.open_serial_window,
            0,
            4,
            width_px=104,
            padx=(6, 6),
            pady=0,
            sticky="w",
        )

        ttk.Label(
            toolbar,
            textvariable=self.serial_status,
            width=20,
            anchor="w",
        ).grid(row=0, column=5, sticky="ew", padx=(0, 10))

        self.safety_notes_button, self.safety_notes_text_item = self._top_action_button(
            toolbar,
            "Help...",
            self.toggle_safety_notes,
            0,
            6,
            width_px=84,
            padx=(0, 0),
            pady=0,
            sticky="e",
        )

        lcd_box = ttk.LabelFrame(top, text="LCD display")
        lcd_box.grid(row=1, column=0, sticky="n", padx=4, pady=(2, 6))
        lcd_box.columnconfigure(0, weight=1)

        # LCD stays stacked like the real display.
        # Width is intentionally close to the real 24-character LCD.
        self._lcd_dual_display(lcd_box, 0, 0)

        lcd_actions = ttk.Frame(lcd_box)
        lcd_actions.grid(row=1, column=0, sticky="", padx=8, pady=(2, 6))
        self._top_action_button(
            lcd_actions,
            "Refresh LCD",
            self.refresh_lcd,
            0,
            0,
            width_px=104,
            padx=(0, 8),
        )
        ttk.Checkbutton(
            lcd_actions,
            text="HTTP fallback",
            variable=self.auto_lcd,
        ).grid(row=0, column=1)

        self._set_led_state("SERIAL_ACTIVE", "RED")

        # Keep local hold LEDs initialized, but do not show a textual Held row.
        self._update_held_status()

        # Serial details/log is now opened as a separate diagnostic window.
        # Keep the main remote panel compact.
        self.serial_details_frame = None
        self.serial_log_text = None

        controls = ttk.LabelFrame(root, text="Remote keyboard views")
        controls.grid(row=1, column=0, sticky="ew", padx=10, pady=6)
        controls.columnconfigure(0, weight=1)

        tabs = ttk.Notebook(controls)
        tabs.grid(row=0, column=0, sticky="ew", padx=6, pady=6)

        current_tab = ttk.Frame(tabs)
        original_tab = ttk.Frame(tabs)

        tabs.add(current_tab, text="MiniJV880 current hardware")
        tabs.add(original_tab, text="Original JV-880 panel")

        # --------------------------------------------------------------
        # MiniJV880 current hardware
        # --------------------------------------------------------------
        current_tab.columnconfigure(0, weight=1)

        current_main = ttk.LabelFrame(current_tab, text="Main keys")
        current_main.grid(row=0, column=0, sticky="ew", padx=6, pady=6)
        current_main.columnconfigure(0, weight=1)

        current_main_inner = ttk.Frame(current_main)
        current_main_inner.grid(row=0, column=0, padx=4, pady=4)

        for col in range(3):
            current_main_inner.columnconfigure(col, weight=0)

        current_main_button_w = 16
        current_main_button_h = 2

        self._key_button(current_main_inner, "P.INFO", lambda: self.tap("TONE3"), 0, 0, width=current_main_button_w, height=current_main_button_h)
        self._key_button(current_main_inner, "UTILITY", lambda: self.tap("UTILITY"), 0, 1, width=current_main_button_w, height=current_main_button_h)
        self._key_button(current_main_inner, "SYSTEM", lambda: self.tap("SYSTEM"), 0, 2, width=current_main_button_w, height=current_main_button_h)

        self._key_button(current_main_inner, "<--", lambda: self.tap("LEFT"), 1, 0, width=current_main_button_w, height=current_main_button_h)
        self._preview_hold_button(
            current_main_inner,
            "PREVIEW",
            1,
            1,
            width_px=max(88, current_main_button_w * 8 + 22),
            height_px=max(44, current_main_button_h * 15 + 18),
        )
        self._key_button(current_main_inner, "-->", lambda: self.tap("RIGHT"), 1, 2, width=current_main_button_w, height=current_main_button_h)

        self._key_button(current_main_inner, "PATCH\nPERF", lambda: self.tap("PATCHPERF"), 2, 0, width=current_main_button_w, height=current_main_button_h)
        self._key_button(current_main_inner, "EDIT", lambda: self.tap("EDIT"), 2, 1, width=current_main_button_w, height=current_main_button_h)
        self._key_button(current_main_inner, "TONE\nSELECT", lambda: self.tap("TONESEL"), 2, 2, width=current_main_button_w, height=current_main_button_h)

        self._key_button(current_main_inner, "RHYTHM", lambda: self.tap("RHYTHM"), 3, 0, width=current_main_button_w, height=current_main_button_h)
        self._key_button(current_main_inner, "MUTE", lambda: self.tap("TONE1"), 3, 1, width=current_main_button_w, height=current_main_button_h)
        self._key_button(current_main_inner, "MONITOR", lambda: self.tap("TONE2"), 3, 2, width=current_main_button_w, height=current_main_button_h)

        current_ops = ttk.LabelFrame(current_tab, text="DATA / ENTER / Encoder")
        current_ops.grid(row=1, column=0, sticky="ew", padx=6, pady=6)
        current_ops.columnconfigure(0, weight=1)

        current_ops_inner = ttk.Frame(current_ops)
        current_ops_inner.grid(row=0, column=0, padx=4, pady=4)

        for col in range(3):
            current_ops_inner.columnconfigure(col, weight=0)

        current_ops_button_w = 16
        current_ops_button_h = 4
        current_ops_button_px_w = 150
        current_ops_button_px_h = 78

        self._key_button(current_ops_inner, "DATA\nCCW", lambda: self.encoder("ccw"), 0, 0, width=current_ops_button_w, height=current_ops_button_h)
        self._key_button(current_ops_inner, "DATA\nCW", lambda: self.encoder("cw"), 0, 1, width=current_ops_button_w, height=current_ops_button_h)
        self._key_button(current_ops_inner, "DATA tap\nMiniJV SR", lambda: self.tap("DATA"), 0, 2, width=current_ops_button_w, height=current_ops_button_h)

        self._key_button(current_ops_inner, "ENTER\ntap", lambda: self.tap("ENTER"), 1, 0, width=current_ops_button_w, height=current_ops_button_h)
        self._led_key_button(current_ops_inner, "ENTER_LONG_CURRENT", "ENTER long\nRD-500\nPN-JV80/SYX", self.enter_long, 1, 1, width_px=current_ops_button_px_w, height_px=current_ops_button_px_h)
        self._key_button(current_ops_inner, "DATA hold\nI/C select", self.toggle_data_hold, 1, 2, width=current_ops_button_w, height=current_ops_button_h)

        # --------------------------------------------------------------
        # Original JV-880 panel / experimental
        # --------------------------------------------------------------
        original_tab.columnconfigure(0, weight=1)

        original_button_w = 132
        original_button_h = 72
        original_button_pad_x = 3
        original_button_pad_y = 3

        original_main = ttk.LabelFrame(original_tab, text="Main switch board")
        original_main.grid(row=0, column=0, sticky="ew", padx=6, pady=6)
        original_main.columnconfigure(0, weight=1)

        original_main_matrix = ttk.Frame(original_main)
        original_main_matrix.grid(row=0, column=0, padx=4, pady=4)

        self._led_key_button(original_main_matrix, "PATCHPERF", "PATCH\nPERFORMANCE", lambda: self.tap("PATCHPERF"), 0, 0, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "EDIT", "EDIT", lambda: self.tap("EDIT"), 0, 1, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "SYSTEM", "SYSTEM", lambda: self.tap("SYSTEM"), 0, 2, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "RHYTHM", "RHYTHM", lambda: self.tap("RHYTHM"), 0, 3, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "UTILITY", "UTILITY", lambda: self.tap("UTILITY"), 0, 4, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)

        self._led_key_button(original_main_matrix, "TONESEL_HOLD", "TONE SELECT\nPARAM SHIFT", self.toggle_tone_select_hold, 1, 0, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "TONE1", "TONE SW 1\nMUTE", lambda: self.tap("TONE1"), 1, 1, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "TONE2", "TONE SW 2\nMONITOR", lambda: self.tap("TONE2"), 1, 2, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "TONE3", "TONE SW 3\nINFO / COMPARE", lambda: self.tap("TONE3"), 1, 3, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_main_matrix, "TONE4", "TONE SW 4\nENTER", lambda: self.tap("ENTER"), 1, 4, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)

        original_helpers = ttk.LabelFrame(original_tab, text="Cursor / Param macros")
        original_helpers.grid(row=1, column=0, sticky="ew", padx=6, pady=6)
        original_helpers.columnconfigure(0, weight=1)

        original_helpers_matrix = ttk.Frame(original_helpers)
        original_helpers_matrix.grid(row=0, column=0, padx=4, pady=4)

        self._plain_panel_button(original_helpers_matrix, "CURSOR\n<", lambda: self.tap("LEFT"), 0, 0, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._plain_panel_button(original_helpers_matrix, "CURSOR\n>", lambda: self.tap("RIGHT"), 0, 1, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._plain_panel_button(original_helpers_matrix, "PARAM\n<", lambda: self.param_shift_cursor("left"), 0, 2, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._plain_panel_button(original_helpers_matrix, "PARAM\n>", lambda: self.param_shift_cursor("right"), 0, 3, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_helpers_matrix, "ENTER_LONG_ORIGINAL", "ENTER long\nRD-500\nPN-JV80/SYX", self.enter_long, 0, 4, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)

        original_data = ttk.LabelFrame(original_tab, text="DATA dial / VOLUME / PREVIEW")
        original_data.grid(row=2, column=0, sticky="ew", padx=6, pady=6)
        original_data.columnconfigure(0, weight=1)

        original_data_matrix = ttk.Frame(original_data)
        original_data_matrix.grid(row=0, column=0, padx=4, pady=4)

        self._plain_panel_button(original_data_matrix, "DATA\nCCW", lambda: self.encoder("ccw"), 0, 0, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._plain_panel_button(original_data_matrix, "DATA\nCW", lambda: self.encoder("cw"), 0, 1, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._led_key_button(original_data_matrix, "DATA_HOLD", "DATA hold\nI/C select", self.toggle_data_hold, 0, 2, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._plain_panel_button(original_data_matrix, "DATA tap\nMiniJV SR", lambda: self.tap("DATA"), 0, 3, width_px=original_button_w, height_px=original_button_h, padx=original_button_pad_x, pady=original_button_pad_y)
        self._preview_hold_button(
            original_data_matrix,
            "VOLUME press\nPREVIEW",
            0,
            4,
            width_px=original_button_w,
            height_px=original_button_h,
            padx=original_button_pad_x,
            pady=original_button_pad_y,
        )


        help_bar = ttk.Frame(root)
        help_bar.grid(row=2, column=0, sticky="ew", padx=10, pady=(0, 6))
        help_bar.columnconfigure(0, weight=1)


        # Safety notes are now opened as a separate help window.
        # Keep the main remote panel compact.
        self.safety_notes_frame = None

    def _set_safety_notes_button_label(self, label: str) -> None:
        try:
            if isinstance(self.safety_notes_button, tk.Canvas) and self.safety_notes_text_item is not None:
                if self.safety_notes_button.winfo_exists():
                    self.safety_notes_button.itemconfigure(self.safety_notes_text_item, text=label)
            elif self.safety_notes_button is not None and self.safety_notes_button.winfo_exists():
                self.safety_notes_button.configure(text=label)
        except Exception:
            pass

    def open_display_settings_window(self) -> None:
        if self.display_settings_window is not None:
            try:
                if self.display_settings_window.winfo_exists():
                    win = self.display_settings_window
                    self.display_settings_window = None
                    win.destroy()
                    return
            except Exception:
                pass
            self.display_settings_window = None

        win = tk.Toplevel(self.root)
        win.title("MiniJV880 Display settings")
        win.transient(self.root)
        win.columnconfigure(0, weight=1)

        self.display_settings_window = win

        def on_close() -> None:
            self.display_settings_window = None
            win.destroy()

        def combo_current_label(
            key: str,
            presets,
            fallback: str,
        ) -> str:
            preset = presets.get(key)
            if preset is None:
                return fallback
            if isinstance(preset, tuple):
                return str(preset[0])
            return str(preset)

        def key_for_label(label: str, presets) -> str | None:
            for preset_key, preset in presets.items():
                preset_label = preset[0] if isinstance(preset, tuple) else preset
                if str(preset_label) == label:
                    return str(preset_key)
            return None

        def make_readonly_combo(
            parent,
            *,
            row: int,
            label: str,
            values: list[str],
            current: str,
            on_selected,
        ) -> None:
            ttk.Label(parent, text=label).grid(
                row=row,
                column=0,
                sticky="w",
                padx=8,
                pady=(6, 2),
            )

            selected = tk.StringVar(value=current)
            combo = ttk.Combobox(
                parent,
                textvariable=selected,
                values=values,
                state="readonly",
                width=28,
            )
            combo.grid(
                row=row,
                column=1,
                sticky="ew",
                padx=8,
                pady=(6, 2),
            )

            def handle_selected(_event=None) -> None:
                on_selected(selected.get())

            combo.bind("<<ComboboxSelected>>", handle_selected)

        win.protocol("WM_DELETE_WINDOW", on_close)

        box = ttk.LabelFrame(win, text="Display")
        box.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        box.columnconfigure(1, weight=1)

        color_values = [preset[0] for preset in self.lcd_color_presets.values()]
        make_readonly_combo(
            box,
            row=0,
            label="LCD color",
            values=color_values,
            current=combo_current_label(
                self.lcd_color_preset.get(),
                self.lcd_color_presets,
                color_values[0],
            ),
            on_selected=lambda label: (
                self.apply_lcd_color_preset(key)
                if (key := key_for_label(label, self.lcd_color_presets)) is not None
                else None
            ),
        )

        tone_values = [preset[0] for preset in self.lcd_background_tone_presets.values()]
        make_readonly_combo(
            box,
            row=1,
            label="Background tone",
            values=tone_values,
            current=combo_current_label(
                self.lcd_background_tone.get(),
                self.lcd_background_tone_presets,
                tone_values[2],
            ),
            on_selected=lambda label: (
                self.apply_lcd_background_tone(key)
                if (key := key_for_label(label, self.lcd_background_tone_presets)) is not None
                else None
            ),
        )

        render_values = list(self.lcd_render_mode_presets.values())
        make_readonly_combo(
            box,
            row=2,
            label="Character style",
            values=render_values,
            current=combo_current_label(
                self.lcd_render_mode.get(),
                self.lcd_render_mode_presets,
                render_values[0],
            ),
            on_selected=lambda label: (
                self.apply_lcd_render_mode(key)
                if (key := key_for_label(label, self.lcd_render_mode_presets)) is not None
                else None
            ),
        )

        text_values = [preset[0] for preset in self.lcd_text_color_presets.values()]
        make_readonly_combo(
            box,
            row=3,
            label="Text color",
            values=text_values,
            current=combo_current_label(
                self.lcd_text_color_preset.get(),
                self.lcd_text_color_presets,
                text_values[0],
            ),
            on_selected=lambda label: (
                self.apply_lcd_text_color_preset(key)
                if (key := key_for_label(label, self.lcd_text_color_presets)) is not None
                else None
            ),
        )

        ttk.Checkbutton(
            box,
            text="Invert background/text",
            variable=self.lcd_invert_colors,
            command=self.apply_lcd_invert_colors,
        ).grid(
            row=4,
            column=0,
            columnspan=2,
            sticky="w",
            padx=8,
            pady=(10, 6),
        )


    def apply_lcd_render_mode(self, mode_key: str | None = None) -> None:
        key = mode_key or self.lcd_render_mode.get()
        if key not in self.lcd_render_mode_presets:
            return

        self.lcd_render_mode.set(key)
        self._update_lcd_canvas()

    def _default_lcd_text_color(self) -> str:
        preset = self.lcd_color_presets.get(self.lcd_color_preset.get())
        if preset is None:
            return "#000000"

        _label, _bg, recommended_fg, _outer_bg, _bezel_bg = preset
        return recommended_fg

    def _apply_lcd_text_color(self) -> None:
        key = self.lcd_text_color_preset.get()
        preset = self.lcd_text_color_presets.get(key)
        if preset is None:
            key = "auto"
            self.lcd_text_color_preset.set(key)
            preset = self.lcd_text_color_presets[key]

        _label, color = preset
        self.lcd_fg = color if color is not None else self._default_lcd_text_color()

    def _adjust_hex_color(self, color: str, amount: float) -> str:
        value = color.strip()
        if len(value) != 7 or not value.startswith("#"):
            return color

        try:
            red = int(value[1:3], 16)
            green = int(value[3:5], 16)
            blue = int(value[5:7], 16)
        except Exception:
            return color

        def adjust(channel: int) -> int:
            if amount < 0:
                return max(0, min(255, round(channel * (1.0 + amount))))
            return max(0, min(255, round(channel + (255 - channel) * amount)))

        return f"#{adjust(red):02x}{adjust(green):02x}{adjust(blue):02x}"

    def _lcd_toned_background(self) -> str:
        preset = self.lcd_background_tone_presets.get(self.lcd_background_tone.get())
        if preset is None:
            return self.lcd_bg

        _label, amount = preset
        return self._adjust_hex_color(self.lcd_bg, float(amount))

    def _lcd_effective_colors(self) -> tuple[str, str]:
        # Returns the actual Canvas background and character foreground.
        toned_bg = self._lcd_toned_background()

        if self.lcd_invert_colors.get():
            return self.lcd_fg, toned_bg

        return toned_bg, self.lcd_fg

    def _configure_lcd_canvas_colors(self) -> None:
        if self.lcd_canvas is None:
            return

        effective_bg, _effective_fg = self._lcd_effective_colors()
        try:
            self.lcd_canvas.configure(bg=effective_bg)
        except Exception:
            pass

    def apply_lcd_background_tone(self, tone_key: str | None = None) -> None:
        key = tone_key or self.lcd_background_tone.get()
        if key not in self.lcd_background_tone_presets:
            return

        self.lcd_background_tone.set(key)
        self._configure_lcd_canvas_colors()
        self._update_lcd_canvas()

    def apply_lcd_invert_colors(self, value: bool | None = None) -> None:
        if value is not None:
            self.lcd_invert_colors.set(bool(value))

        self._configure_lcd_canvas_colors()
        self._update_lcd_canvas()

    def apply_lcd_text_color_preset(self, preset_key: str | None = None) -> None:
        key = preset_key or self.lcd_text_color_preset.get()
        if key not in self.lcd_text_color_presets:
            return

        self.lcd_text_color_preset.set(key)
        self._apply_lcd_text_color()
        self._update_lcd_canvas()

    def apply_lcd_color_preset(self, preset_key: str | None = None) -> None:
        key = preset_key or self.lcd_color_preset.get()
        preset = self.lcd_color_presets.get(key)
        if preset is None:
            return

        _label, bg, _recommended_fg, outer_bg, bezel_bg = preset
        self.lcd_color_preset.set(key)
        self.lcd_bg = bg
        self.lcd_outer_bg = outer_bg
        self.lcd_bezel_bg = bezel_bg
        self._apply_lcd_text_color()

        if self.lcd_outer_frame is not None:
            try:
                self.lcd_outer_frame.configure(bg=self.lcd_outer_bg)
            except Exception:
                pass

        if self.lcd_bezel_frame is not None:
            try:
                self.lcd_bezel_frame.configure(bg=self.lcd_bezel_bg)
            except Exception:
                pass

        self._configure_lcd_canvas_colors()
        self._update_lcd_canvas()

    def toggle_safety_notes(self) -> None:
        if self.safety_notes_window is not None:
            try:
                if self.safety_notes_window.winfo_exists():
                    self._close_safety_notes_window()
                    return
            except Exception:
                pass
            self.safety_notes_window = None

        self._open_safety_notes_window()

    def _open_safety_notes_window(self) -> None:
        win = tk.Toplevel(self.root)
        win.title("MiniJV880 Safety notes")
        win.transient(self.root)
        win.columnconfigure(0, weight=1)

        self.safety_notes_window = win
        self.safety_notes_visible = True

        win.protocol("WM_DELETE_WINDOW", self._close_safety_notes_window)

        box = ttk.LabelFrame(win, text="Safety notes")
        box.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        box.columnconfigure(0, weight=1)

        ttk.Label(
            box,
            text=(
                "This prototype does not use periodic HTTP polling. "
                "LCD HTTP readback is used at startup, by Refresh LCD, and as an event-driven fallback after remote commands. "
                "When the serial monitor is active, HTTP LCD fallback is skipped. "
                "The serial monitor is passive PC-side readback of LCD/LED events. "
                "LED state is synchronized once via /rled.txt when serial monitoring starts."
            ),
            wraplength=760,
            justify="left",
        ).grid(row=0, column=0, sticky="ew", padx=6, pady=6)

        self._set_safety_notes_button_label("Hide help")

    def _close_safety_notes_window(self) -> None:
        win = self.safety_notes_window
        self.safety_notes_window = None
        self.safety_notes_frame = None
        self.safety_notes_visible = False

        if win is not None:
            try:
                if win.winfo_exists():
                    win.destroy()
            except Exception:
                pass

        self._set_safety_notes_button_label("Help...")

    # ------------------------------------------------------------------
    # HTTP helpers
    # ------------------------------------------------------------------
    def _base(self) -> str:
        return self.base_url.get().strip().rstrip("/")

    def _timeout(self) -> float:
        try:
            value = float(self.timeout_s.get())
            return max(0.2, min(value, 10.0))
        except Exception:
            return 2.0

    def get_path(self, path: str, callback=None, auto_refresh_after: bool = False, auto_delay_ms: int = 800) -> None:
        url = self._base() + path
        self.status.set(url)

        def worker() -> None:
            ok = False
            text = ""
            try:
                with request.urlopen(url, timeout=self._timeout()) as response:
                    data = response.read(4096)
                text = data.decode("iso-8859-1", errors="replace")
                ok = True
            except error.URLError as exc:
                text = f"HTTP error: {exc}"
            except Exception as exc:
                text = f"Error: {exc}"

            self.root.after(
                0,
                lambda: self._request_done(ok, text, callback, auto_refresh_after, auto_delay_ms),
            )

        threading.Thread(target=worker, daemon=True).start()

    def _request_done(self, ok: bool, text: str, callback, auto_refresh_after: bool, auto_delay_ms: int) -> None:
        if callback is not None:
            callback(ok, text)
        else:
            self.status.set("OK" if ok else text)

        if ok and auto_refresh_after:
            self.schedule_lcd_refresh(auto_delay_ms)

    def open_connection_window(self) -> None:
        if self.connection_window is not None:
            try:
                if self.connection_window.winfo_exists():
                    win = self.connection_window
                    self.connection_window = None
                    win.destroy()
                    return
            except Exception:
                pass
            self.connection_window = None

        win = tk.Toplevel(self.root)
        win.title("MiniJV880 Connection / status")
        win.transient(self.root)
        win.columnconfigure(0, weight=1)

        self.connection_window = win

        def on_close() -> None:
            self.connection_window = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", on_close)

        box = ttk.LabelFrame(win, text="Connection / status")
        box.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        box.columnconfigure(1, weight=1)

        ttk.Label(box, text="URL:").grid(row=0, column=0, sticky="w", padx=(6, 4), pady=(6, 2))
        ttk.Entry(
            box,
            textvariable=self.base_url,
            width=36,
        ).grid(row=0, column=1, columnspan=3, sticky="ew", padx=(0, 6), pady=(6, 2))

        ttk.Label(box, text="HTTP timeout:").grid(row=1, column=0, sticky="w", padx=(6, 4), pady=2)
        ttk.Entry(
            box,
            textvariable=self.timeout_s,
            width=6,
        ).grid(row=1, column=1, sticky="w", padx=(0, 8), pady=2)

        ttk.Label(box, text="Status:").grid(row=2, column=0, sticky="w", padx=(6, 4), pady=(2, 6))
        ttk.Label(
            box,
            textvariable=self.status,
            width=38,
            anchor="w",
        ).grid(row=2, column=1, columnspan=3, sticky="ew", padx=(0, 6), pady=(2, 6))

        actions = ttk.Frame(box)
        actions.grid(row=3, column=0, columnspan=4, sticky="ew", padx=6, pady=(2, 6))
        actions.columnconfigure(3, weight=1)

        self._top_action_button(
            actions,
            "Refresh LCD",
            self.refresh_lcd,
            0,
            0,
            width_px=104,
            padx=(0, 8),
        )

        self._top_action_button(
            actions,
            "Clear remote",
            self.clear_remote,
            0,
            1,
            width_px=104,
            padx=(0, 8),
        )

        ttk.Checkbutton(
            actions,
            text="HTTP fallback",
            variable=self.auto_lcd,
        ).grid(row=0, column=2, sticky="w")

    def open_serial_window(self) -> None:
        if self.serial_window is not None:
            try:
                if self.serial_window.winfo_exists():
                    win = self.serial_window
                    self.serial_window = None
                    win.destroy()
                    return
            except Exception:
                pass
            self.serial_window = None

        win = tk.Toplevel(self.root)
        win.title("MiniJV880 Serial monitor / remote state")
        win.transient(self.root)
        win.columnconfigure(0, weight=1)

        self.serial_window = win

        def on_close() -> None:
            self.serial_window = None
            win.destroy()

        win.protocol("WM_DELETE_WINDOW", on_close)

        box = ttk.LabelFrame(win, text="Serial monitor / remote state")
        box.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        box.columnconfigure(0, weight=1)

        serial_io_row = ttk.Frame(box)
        serial_io_row.grid(row=0, column=0, sticky="w", padx=6, pady=(6, 2))

        ttk.Label(serial_io_row, text="Port:").grid(row=0, column=0, sticky="w", padx=(0, 4))
        ttk.Entry(
            serial_io_row,
            textvariable=self.serial_port,
            width=18,
        ).grid(row=0, column=1, sticky="w", padx=(0, 12))

        ttk.Label(serial_io_row, text="Baud:").grid(row=0, column=2, sticky="w", padx=(0, 4))
        ttk.Entry(
            serial_io_row,
            textvariable=self.serial_baud,
            width=8,
        ).grid(row=0, column=3, sticky="w")

        serial_actions_row = ttk.Frame(box)
        serial_actions_row.grid(row=1, column=0, sticky="ew", padx=6, pady=2)
        serial_actions_row.columnconfigure(3, weight=1)

        self._serial_toggle_led_button(serial_actions_row, 0, 0)

        self.serial_details_button, self.serial_details_text_item = self._top_action_button(
            serial_actions_row,
            "Hide details" if self.serial_details_visible else "Show details",
            self.toggle_serial_details,
            0,
            1,
            width_px=104,
            padx=(8, 0),
        )

        self._top_action_button(
            serial_actions_row,
            "Clear remote",
            self.clear_remote,
            0,
            4,
            width_px=104,
            sticky="e",
        )

        ttk.Label(
            box,
            textvariable=self.serial_status,
            width=44,
            anchor="w",
        ).grid(row=2, column=0, sticky="ew", padx=6, pady=(2, 6))

        self._refresh_serial_toggle()

        minicom_row = ttk.Frame(box)
        minicom_row.grid(
            row=99,
            column=0,
            sticky="ew",
            padx=6,
            pady=(4, 6),
        )
        minicom_row.columnconfigure(2, weight=1)

        self.minicom_button, self.minicom_button_text_item = (
            self._top_action_button(
                minicom_row,
                (
                    "Close minicom"
                    if self.minicom_bridge_master_fd is not None
                    else "Open minicom"
                ),
                self.toggle_minicom_bridge,
                0,
                0,
                width_px=112,
                padx=(0, 10),
            )
        )

        ttk.Checkbutton(
            minicom_row,
            text="Mirror raw serial to shell",
            variable=self.serial_stdout_mirror,
        ).grid(
            row=0,
            column=1,
            sticky="w",
            padx=(0, 12),
        )

        ttk.Label(
            minicom_row,
            text=f"Minicom PTY: {self.minicom_bridge_link}",
            anchor="w",
        ).grid(
            row=0,
            column=2,
            sticky="ew",
        )

    def _set_serial_details_button_label(self, label: str) -> None:
        try:
            if isinstance(self.serial_details_button, tk.Canvas) and self.serial_details_text_item is not None:
                if self.serial_details_button.winfo_exists():
                    self.serial_details_button.itemconfigure(self.serial_details_text_item, text=label)
            elif self.serial_details_button is not None and self.serial_details_button.winfo_exists():
                self.serial_details_button.configure(text=label)
        except Exception:
            pass

    def toggle_serial_details(self) -> None:
        if self.serial_details_window is not None:
            try:
                if self.serial_details_window.winfo_exists():
                    self._close_serial_details_window()
                    return
            except Exception:
                pass
            self.serial_details_window = None

        self._open_serial_details_window()

    def _open_serial_details_window(self) -> None:
        win = tk.Toplevel(self.root)
        win.title("MiniJV880 Serial details")
        win.transient(self.root)
        win.columnconfigure(0, weight=1)
        win.rowconfigure(0, weight=1)

        self.serial_details_window = win
        self.serial_details_visible = True

        win.protocol("WM_DELETE_WINDOW", self._close_serial_details_window)

        frame = ttk.Frame(win)
        frame.grid(row=0, column=0, sticky="nsew", padx=10, pady=10)
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        self.serial_details_frame = frame
        self.serial_log_text = tk.Text(frame, height=12, width=96, wrap="none")
        self.serial_log_text.grid(row=0, column=0, sticky="nsew")

        serial_scroll_y = ttk.Scrollbar(frame, orient="vertical", command=self.serial_log_text.yview)
        serial_scroll_y.grid(row=0, column=1, sticky="ns")

        serial_scroll_x = ttk.Scrollbar(frame, orient="horizontal", command=self.serial_log_text.xview)
        serial_scroll_x.grid(row=1, column=0, sticky="ew")

        self.serial_log_text.configure(
            yscrollcommand=serial_scroll_y.set,
            xscrollcommand=serial_scroll_x.set,
        )

        self._set_serial_details_button_label("Hide details")

    def _close_serial_details_window(self) -> None:
        win = self.serial_details_window
        self.serial_details_window = None
        self.serial_details_frame = None
        self.serial_log_text = None
        self.serial_details_visible = False

        if win is not None:
            try:
                if win.winfo_exists():
                    win.destroy()
            except Exception:
                pass

        self._set_serial_details_button_label("Show details")

    # ------------------------------------------------------------------
    # Serial monitor
    # ------------------------------------------------------------------
    def _refresh_serial_toggle(self) -> None:
        # The serial Start/Stop control can exist in multiple places:
        # compact topbar and optional Serial window. Always update through
        # the multi-instance visual helper, never through the legacy single
        # self.serial_toggle_button reference, which may point to a destroyed
        # Toplevel canvas after the Serial window is closed.
        self._set_led_state("SERIAL_ACTIVE", "GREEN" if self.serial_running else "RED")
        self._apply_serial_toggle_visual()



    def _set_minicom_button_label(self, label: str) -> None:
        button = self.minicom_button
        text_item = self.minicom_button_text_item

        if button is None or text_item is None:
            return

        try:
            if button.winfo_exists():
                button.itemconfigure(text_item, text=label)
        except Exception:
            pass

    def _serial_lock_pid(self, port: str) -> int | None:
        lock_name = f"LCK..{Path(port).name}"

        for directory in (Path("/run/lock"), Path("/var/lock")):
            lock_path = directory / lock_name

            try:
                fields = lock_path.read_text(
                    encoding="ascii",
                    errors="ignore",
                ).strip().split()
            except (FileNotFoundError, PermissionError, OSError):
                continue

            if not fields:
                continue

            try:
                pid = int(fields[0])
            except ValueError:
                continue

            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                continue
            except PermissionError:
                return pid
            else:
                return pid

        return None

    def _serial_port_other_owners(self, port: str) -> list[str]:
        owners: list[str] = []
        current_pid = os.getpid()

        try:
            port_path = Path(port).resolve(strict=True)
        except OSError:
            return owners

        try:
            process_dirs = list(Path("/proc").iterdir())
        except OSError:
            return owners

        for process_dir in process_dirs:
            if not process_dir.name.isdigit():
                continue

            pid = int(process_dir.name)

            if pid == current_pid:
                continue

            fd_dir = process_dir / "fd"

            try:
                fd_entries = list(fd_dir.iterdir())
            except (FileNotFoundError, PermissionError, OSError):
                continue

            owns_port = False

            for fd_path in fd_entries:
                try:
                    if os.path.samefile(fd_path, port_path):
                        owns_port = True
                        break
                except (FileNotFoundError, PermissionError, OSError):
                    continue

            if not owns_port:
                continue

            try:
                process_name = (
                    process_dir / "comm"
                ).read_text(
                    encoding="utf-8",
                    errors="replace",
                ).strip()
            except (FileNotFoundError, PermissionError, OSError):
                process_name = "unknown"

            owners.append(f"{pid}:{process_name}")

        return owners

    def _pty_client_present(self, slave_path: str) -> bool:
        current_pid = os.getpid()

        try:
            slave = Path(slave_path).resolve(strict=True)
            process_dirs = list(Path("/proc").iterdir())
        except OSError:
            return False

        for process_dir in process_dirs:
            if not process_dir.name.isdigit():
                continue

            if int(process_dir.name) == current_pid:
                continue

            try:
                fd_entries = list((process_dir / "fd").iterdir())
            except (FileNotFoundError, PermissionError, OSError):
                continue

            for fd_path in fd_entries:
                try:
                    if os.path.samefile(fd_path, slave):
                        return True
                except (FileNotFoundError, PermissionError, OSError):
                    continue

        return False

    def _close_minicom_bridge(
        self,
        status_message: str | None = None,
        *,
        terminate_process: bool = True,
    ) -> bool:
        with self.minicom_bridge_lock:
            master_fd = self.minicom_bridge_master_fd
            process = self.minicom_bridge_process

            had_bridge = (
                master_fd is not None
                or process is not None
                or self.minicom_bridge_link.is_symlink()
            )

            self.minicom_bridge_master_fd = None
            self.minicom_bridge_connected = False
            self.minicom_bridge_process = None
            self.minicom_bridge_monitor_thread = None

        if (
            terminate_process
            and process is not None
            and process.poll() is None
        ):
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            except OSError:
                try:
                    process.terminate()
                except OSError:
                    pass

        if master_fd is not None:
            try:
                os.close(master_fd)
            except OSError:
                pass

        try:
            if self.minicom_bridge_link.is_symlink():
                self.minicom_bridge_link.unlink()
                had_bridge = True
        except OSError:
            pass

        def update_ui() -> None:
            self._set_minicom_button_label("Open minicom")

            if status_message is not None and had_bridge:
                self.serial_status.set(status_message)

        try:
            self.root.after(0, update_ui)
        except Exception:
            pass

        return had_bridge

    def _drain_minicom_input(self, master_fd: int) -> None:
        # GPIO4 is a one-way MiniJV880 -> PC debug connection.
        # Discard anything typed inside minicom so PTY input cannot fill.
        while True:
            try:
                data = os.read(master_fd, 4096)
            except InterruptedError:
                continue
            except BlockingIOError:
                return
            except OSError as exc:
                if exc.errno in (
                    errno.EAGAIN,
                    errno.EWOULDBLOCK,
                    errno.EIO,
                ):
                    return
                return

            if not data:
                return

    def _mirror_serial_to_minicom(self, payload: bytes) -> None:
        with self.minicom_bridge_lock:
            if not self.minicom_bridge_connected:
                return

            master_fd = self.minicom_bridge_master_fd

        if master_fd is None:
            return

        remaining = memoryview(payload)

        while remaining:
            try:
                written = os.write(master_fd, remaining)
            except InterruptedError:
                continue
            except BlockingIOError:
                return
            except OSError as exc:
                if exc.errno not in (
                    errno.EAGAIN,
                    errno.EWOULDBLOCK,
                ):
                    self._close_minicom_bridge(
                        "Minicom bridge disconnected"
                    )
                return

            if written <= 0:
                return

            remaining = remaining[written:]

    def _minicom_bridge_monitor(
        self,
        master_fd: int,
        slave_path: str,
        process: subprocess.Popen,
    ) -> None:
        deadline = time.monotonic() + 12.0
        connected = False

        while process.poll() is None:
            with self.minicom_bridge_lock:
                if (
                    self.minicom_bridge_master_fd != master_fd
                    or self.minicom_bridge_process is not process
                ):
                    return

            if not connected:
                if self._pty_client_present(slave_path):
                    connected = True

                    with self.minicom_bridge_lock:
                        if (
                            self.minicom_bridge_master_fd != master_fd
                            or self.minicom_bridge_process is not process
                        ):
                            return

                        self.minicom_bridge_connected = True

                    try:
                        self.root.after(
                            0,
                            lambda: self.serial_status.set(
                                "Minicom connected via "
                                f"{self.minicom_bridge_link}"
                            ),
                        )
                    except Exception:
                        pass

                elif time.monotonic() >= deadline:
                    self._close_minicom_bridge(
                        "Minicom did not connect; bridge stopped"
                    )
                    return

            else:
                self._drain_minicom_input(master_fd)

            time.sleep(0.10)

        if connected:
            message = "Minicom closed; bridge stopped"
        else:
            message = "Minicom exited before connecting"

        self._close_minicom_bridge(
            message,
            terminate_process=False,
        )

    def toggle_minicom_bridge(self) -> None:
        with self.minicom_bridge_lock:
            bridge_active = self.minicom_bridge_master_fd is not None

        if bridge_active:
            self._close_minicom_bridge(
                "Minicom bridge stopped"
            )
        else:
            self.open_minicom_bridge()

    def open_minicom_bridge(self) -> None:
        if not self.serial_open:
            self.serial_status.set(
                "Start serial and wait for the listening state first"
            )
            return

        terminal = shutil.which("xterm")
        minicom = shutil.which("minicom")

        if terminal is None:
            self.serial_status.set(
                "xterm is required to open the minicom log window"
            )
            return

        if minicom is None:
            self.serial_status.set(
                "minicom is not installed"
            )
            return

        try:
            baud = int(self.serial_baud.get())
        except Exception:
            baud = 38400

        with self.minicom_bridge_lock:
            if self.minicom_bridge_master_fd is not None:
                self.serial_status.set(
                    "Minicom bridge is already active"
                )
                return

        link = self.minicom_bridge_link

        try:
            if link.is_symlink():
                link.unlink()
            elif link.exists():
                self.serial_status.set(
                    f"Cannot replace existing file: {link}"
                )
                return
        except OSError as exc:
            self.serial_status.set(
                f"Cannot prepare minicom PTY link: {exc}"
            )
            return

        master_fd = -1
        slave_fd = -1
        process: subprocess.Popen | None = None

        try:
            master_fd, slave_fd = os.openpty()
            tty.setraw(slave_fd)
            os.set_blocking(master_fd, False)

            slave_path = os.ttyname(slave_fd)
            link.symlink_to(slave_path)

            command = [
                terminal,
                "-T",
                "MiniJV880 serial log",
                "-geometry",
                "100x28",
                "-e",
                minicom,
                "-D",
                str(link),
                "-b",
                str(baud),
                "-8",
                "-o",
            ]

            process = subprocess.Popen(
                command,
                close_fds=True,
                start_new_session=True,
            )

            os.close(slave_fd)
            slave_fd = -1

            with self.minicom_bridge_lock:
                self.minicom_bridge_master_fd = master_fd
                self.minicom_bridge_connected = False
                self.minicom_bridge_process = process

            master_fd = -1

            self._set_minicom_button_label("Close minicom")
            self.serial_status.set(
                f"Waiting for minicom on {link}"
            )

            self.minicom_bridge_monitor_thread = threading.Thread(
                target=self._minicom_bridge_monitor,
                args=(
                    self.minicom_bridge_master_fd,
                    slave_path,
                    process,
                ),
                daemon=True,
            )
            self.minicom_bridge_monitor_thread.start()

        except Exception as exc:
            if (
                process is not None
                and process.poll() is None
            ):
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                except OSError:
                    try:
                        process.terminate()
                    except OSError:
                        pass

            if slave_fd >= 0:
                try:
                    os.close(slave_fd)
                except OSError:
                    pass

            if master_fd >= 0:
                try:
                    os.close(master_fd)
                except OSError:
                    pass

            try:
                if link.is_symlink():
                    link.unlink()
            except OSError:
                pass

            self.serial_status.set(
                f"Cannot start minicom bridge: {exc}"
            )

    def toggle_serial_monitor(self) -> None:
        if self.serial_running:
            self.stop_serial_monitor()
        else:
            self.start_serial_monitor()

    def start_serial_monitor(self) -> None:
        if serial is None:
            self.serial_status.set("pyserial not available: install python3-serial or pyserial")
            return

        if self.serial_running:
            self.serial_status.set("Serial monitor already running")
            return

        port = self.serial_port.get().strip()
        if not port:
            self.serial_status.set("Serial port is empty")
            return

        if (
            self.serial_thread is not None
            and self.serial_thread.is_alive()
        ):
            self.serial_status.set(
                "Serial monitor is still stopping"
            )
            return

        busy_reasons: list[str] = []

        lock_pid = self._serial_lock_pid(port)
        if lock_pid is not None:
            busy_reasons.append(f"lock PID {lock_pid}")

        busy_reasons.extend(
            self._serial_port_other_owners(port)
        )

        if busy_reasons:
            details = ", ".join(busy_reasons)
            self.serial_status.set(
                f"{port} is already in use ({details}); "
                "close direct minicom first"
            )
            return

        try:
            baud = int(self.serial_baud.get())
        except Exception:
            baud = 38400
            self.serial_baud.set(baud)

        self.serial_running = True
        self._refresh_serial_toggle()
        self.serial_status.set(f"Opening {port} @ {baud}...")

        self.serial_thread = threading.Thread(
            target=self._serial_worker,
            args=(port, baud),
            daemon=True,
        )
        self.serial_thread.start()

        # One-shot LED sync: serial readback is passive and can miss the initial
        # state if the GUI opens after MiniJV880 has already settled on a screen.
        # Use one small HTTP request instead of periodic serial LED refresh.
        self.root.after(700, self.refresh_leds_once)

    def stop_serial_monitor(self) -> None:
        self.serial_running = False
        self._close_minicom_bridge(
            "Stopping serial; minicom bridge closed"
        )
        self._refresh_serial_toggle()
        self.serial_status.set("Stopping serial monitor...")

    def _serial_worker(self, port: str, baud: int) -> None:
        assert serial is not None
        error_message: str | None = None

        try:
            with serial.Serial(
                port,
                baudrate=baud,
                timeout=0.5,
                exclusive=True,
            ) as ser:
                try:
                    fcntl.ioctl(
                        ser.fileno(),
                        termios.TIOCEXCL,
                    )
                except (AttributeError, OSError):
                    pass

                self.serial_open = True
                self.root.after(
                    0,
                    lambda: self.serial_status.set(
                        f"Serial listening on {port} @ {baud}"
                    ),
                )

                while self.serial_running:
                    raw = ser.readline()
                    if not raw:
                        continue

                    self._mirror_serial_to_minicom(raw)

                    line = raw.decode(
                        "utf-8",
                        errors="replace",
                    ).rstrip("\r\n")
                    self.root.after(
                        0,
                        lambda line=line: self._handle_serial_line(line),
                    )

        except Exception as exc:
            error_message = f"Serial error: {exc}"

        finally:
            self.serial_open = False
            self.serial_running = False
            self._close_minicom_bridge()

            if error_message is None:
                self.root.after(
                    0,
                    lambda: self.serial_status.set(
                        "Serial stopped"
                    ),
                )
            else:
                self.root.after(
                    0,
                    lambda message=error_message: (
                        self.serial_status.set(message)
                    ),
                )

            self.root.after(
                0,
                self._refresh_serial_toggle,
            )

    def _append_serial_tail(self, line: str) -> None:
        if self.serial_log_text is None:
            return

        self.serial_log_text.insert("end", line + "\n")
        self.serial_log_text.see("end")

        # Keep the widget bounded. Delete older lines above roughly 300 lines.
        try:
            line_count = int(float(self.serial_log_text.index("end-1c").split(".")[0]))
            if line_count > 300:
                self.serial_log_text.delete("1.0", "80.0")
        except Exception:
            pass

    def _normalize_led_state(self, value: str) -> str:
        normalized = value.strip().upper()

        if normalized in {"1", "ON", "TRUE", "YES"}:
            return "ON"
        if normalized in {"0", "OFF", "FALSE", "NO"}:
            return "OFF"
        if normalized in {"BLINK", "FLASH", "PULSE"}:
            return "BLINK"
        if normalized in {"UNKNOWN", "UNK", "?"}:
            return "UNKNOWN"

        return "UNKNOWN"

    def _handle_serial_led_line(self, line: str) -> bool:
        if not line.startswith("LED|"):
            return False

        fields: dict[str, str] = {}
        for part in line.split("|")[1:]:
            if "=" not in part:
                continue
            key, value = part.split("=", 1)
            key = key.strip().upper().replace("-", "_").replace("/", "_")
            fields[key] = value.strip()

        if not fields:
            self.serial_status.set("Serial LED line ignored")
            return True

        led_map = {
            "PATCHPERF": "PATCHPERF",
            "PATCH_PERF": "PATCHPERF",
            "PATCH_PERFORMANCE": "PATCHPERF",
            "EDIT": "EDIT",
            "SYSTEM": "SYSTEM",
            "RHYTHM": "RHYTHM",
            "UTILITY": "UTILITY",
            "TONE1": "TONE1",
            "TONE_1": "TONE1",
            "TONE_SW1": "TONE1",
            "TONE2": "TONE2",
            "TONE_2": "TONE2",
            "TONE_SW2": "TONE2",
            "TONE3": "TONE3",
            "TONE_3": "TONE3",
            "TONE_SW3": "TONE3",
            "TONE4": "TONE4",
            "TONE_4": "TONE4",
            "TONE_SW4": "TONE4",
        }

        updated = False

        def apply(led_id: str, state: str) -> None:
            nonlocal updated
            self._set_led_state(led_id, state)
            updated = True

        # Compact Tone Switch form:
        #   TONE=1000 means TONE1 on, TONE2-4 off.
        #   BLINK=0100 makes selected Tone LEDs blink.
        tone_bits = fields.get("TONE")
        if tone_bits:
            for idx, bit in enumerate(tone_bits[:4], start=1):
                apply(f"TONE{idx}", "ON" if bit == "1" else "OFF")

        blink_bits = fields.get("BLINK")
        if blink_bits:
            for idx, bit in enumerate(blink_bits[:4], start=1):
                if bit == "1":
                    apply(f"TONE{idx}", "BLINK")

        # Explicit key=value fields override compact values.
        for key, value in fields.items():
            if key in {"TONE", "BLINK"}:
                continue
            led_id = led_map.get(key)
            if led_id is None:
                continue
            apply(led_id, self._normalize_led_state(value))

        self.serial_status.set("LED updated" if updated else "LED ignored")
        return True

    def _lcd_text_payload(self, payload: str) -> str:
        payload = payload.rstrip()
        if payload.startswith(" "):
            payload = payload[1:]
        return payload

    def _parse_lcd_cursor_fields(self, line: str) -> dict[str, str]:
        fields: dict[str, str] = {}

        if line.startswith("LCDC|"):
            parts = line.split("|")[1:]
        elif line.startswith("CURSOR:"):
            parts = line[len("CURSOR:"):].strip().split()
        else:
            return fields

        for part in parts:
            part = part.strip()
            if not part or "=" not in part:
                continue
            key, value = part.split("=", 1)
            key = key.strip().upper().replace("-", "_").replace("/", "_")
            fields[key] = value.strip()

        return fields

    def _field_as_int(self, fields: dict[str, str], key: str, default: int = 0) -> int:
        value = fields.get(key)
        if value is None:
            return default

        try:
            return int(value, 0)
        except Exception:
            return default

    def _field_as_bool(self, fields: dict[str, str], key: str, default: bool = False) -> bool:
        value = fields.get(key)
        if value is None:
            return default

        normalized = value.strip().upper()
        return normalized in {"1", "ON", "TRUE", "YES", "VISIBLE", "ENABLED"}

    def _handle_lcd_cursor_line(self, line: str, *, source: str = "Serial") -> bool:
        fields = self._parse_lcd_cursor_fields(line)
        if not fields:
            return False

        self.lcd_cursor_row = self._field_as_int(fields, "ROW", self.lcd_cursor_row)
        self.lcd_cursor_col = self._field_as_int(fields, "COL", self.lcd_cursor_col)
        self.lcd_cursor_enabled = self._field_as_bool(fields, "ENABLED", self.lcd_cursor_enabled)
        self.lcd_cursor_visible = self._field_as_bool(fields, "VISIBLE", self.lcd_cursor_enabled)
        self.lcd_cursor_address = self._field_as_int(fields, "ADDR", self.lcd_cursor_address)

        # Local GUI blink is intentionally independent from the MiniJV880 LCD
        # blink phase. When a new cursor position arrives, show it immediately
        # and then let the local blink timer continue.
        self.lcd_cursor_blink_on = True

        self._update_lcd_canvas()

        if source == "Serial":
            self.serial_status.set("Serial LCD cursor updated")

        return True

    def _handle_serial_line(self, line: str) -> None:
        self._append_serial_tail(line)

        # Optional diagnostic mirror. Normal GUI use keeps raw serial
        # data out of the launching shell because minicom receives its own
        # copy through the PTY bridge.
        if self.serial_stdout_mirror.get():
            print(line, flush=True)

        if self._handle_serial_led_line(line):
            return

        if self._handle_lcd_cursor_line(line, source="Serial"):
            return

        # Current firmware/debug format:
        #   LCD1: ...
        #   LCD2: ...
        # Future compact format can be:
        #   LCD|line1|line2
        if line.startswith("LCD|"):
            parts = line.split("|", 2)
            if len(parts) == 3:
                self.lcd1.set(parts[1].rstrip())
                self.lcd2.set(parts[2].rstrip())
                self.serial_status.set("Serial LCD updated")
            return

        lcd1_pos = line.find("LCD1:")
        if lcd1_pos >= 0:
            self._pending_serial_lcd1 = self._lcd_text_payload(line[lcd1_pos + len("LCD1:"):])
            self.serial_status.set("Serial LCD1 received")
            return

        lcd2_pos = line.find("LCD2:")
        if lcd2_pos >= 0:
            line2 = self._lcd_text_payload(line[lcd2_pos + len("LCD2:"):])
            if self._pending_serial_lcd1 is not None:
                self.lcd1.set(self._pending_serial_lcd1)
                self.lcd2.set(line2)
                self._pending_serial_lcd1 = None
                self.serial_status.set("Serial LCD1/LCD2 updated")
            else:
                self.lcd2.set(line2)
                self.serial_status.set("Serial LCD2 updated")
            return


    def _update_held_status(self) -> None:
        def state(name: str) -> str:
            return "HELD" if self.remote_held.get(name, False) else "off"

        self.held_status.set(
            "Held: "
            f"DATA {state('DATA')}   |   "
            f"ENTER {state('ENTER')}   |   "
            f"TONE SELECT {state('TONESEL')}"
        )
        self._refresh_local_leds()

    def _set_enter_long_leds(self, state: str) -> None:
        self._set_led_state("ENTER_LONG_CURRENT", state)
        self._set_led_state("ENTER_LONG_ORIGINAL", state)

    def _clear_enter_long_leds(self) -> None:
        self._enter_long_after_id = None
        self._set_enter_long_leds("OFF")

    def enter_long(self) -> None:
        self._set_enter_long_leds("ON")
        self.press("ENTER", 1500)

        if self._enter_long_after_id is not None:
            try:
                self.root.after_cancel(self._enter_long_after_id)
            except Exception:
                pass

        self._enter_long_after_id = self.root.after(1500, self._clear_enter_long_leds)

    def refresh_leds_once(self) -> None:
        self.get_path("/rled.txt", callback=self._led_done)

    def _led_done(self, ok: bool, text: str) -> None:
        if not ok:
            self.status.set(text)
            return

        for line in text.splitlines():
            line = line.strip()
            if line.startswith("LED|"):
                self._handle_serial_led_line(line)
                self.status.set("LED refreshed")
                return

        self.status.set("LED refresh: no LED line")

    def refresh_lcd(self) -> None:
        self.get_path("/rlcd.txt", callback=self._lcd_done)

    def _lcd_done(self, ok: bool, text: str) -> None:
        if not ok:
            self.status.set(text)
            return

        line1 = ""
        line2 = ""
        cursor_line = ""
        for line in text.splitlines():
            if line.startswith("LCD1:"):
                line1 = self._lcd_text_payload(line[len("LCD1:"):])
            elif line.startswith("LCD2:"):
                line2 = self._lcd_text_payload(line[len("LCD2:"):])
            elif line.startswith("CURSOR:"):
                cursor_line = line.strip()

        self.lcd1.set(line1)
        self.lcd2.set(line2)
        if cursor_line:
            self._handle_lcd_cursor_line(cursor_line, source="HTTP")
        self.status.set("LCD refreshed")

    def _hold_active(self) -> bool:
        return any(self.remote_held.values())

    def schedule_lcd_refresh(self, delay_ms: int = 800) -> None:
        if not self.auto_lcd.get():
            self.status.set("Auto LCD skipped: disabled")
            return
        if self.serial_running:
            self.status.set("LCD serial active")
            return

        if self._hold_active():
            self.status.set("HTTP LCD fallback skipped: remote hold active")
            return

        if self._lcd_after_id is not None:
            self.root.after_cancel(self._lcd_after_id)

        self.status.set(f"Auto LCD refresh scheduled in {delay_ms} ms")

        def later() -> None:
            self._lcd_after_id = None
            if self.auto_lcd.get() and not self._hold_active():
                self.refresh_lcd()
            else:
                self.status.set("Auto LCD refresh skipped")

        self._lcd_after_id = self.root.after(delay_ms, later)

    def toggle_tone_select_hold(self) -> None:
        if self.remote_held.get("TONESEL", False):
            self.up("TONESEL")
            self.status.set("TONE SELECT released")
        else:
            self.down("TONESEL")
            self.status.set("TONE SELECT held")

    def toggle_data_hold(self) -> None:
        if self.remote_held.get("DATA", False):
            self.up("DATA")
            self.status.set("DATA released")
        else:
            self.down("DATA")
            self.status.set("DATA held")

    def param_shift_cursor(self, direction: str) -> None:
        self.down("TONESEL")
        self.root.after(80, lambda: self.tap("LEFT" if direction == "left" else "RIGHT"))
        self.root.after(180, lambda: self.up("TONESEL"))

    # ------------------------------------------------------------------
    # Remote commands
    # ------------------------------------------------------------------
    def raw(self, action: str, mask: str, auto_refresh: bool = True) -> None:
        qs = parse.urlencode({"a": action, "m": mask})
        self.get_path(
            "/rraw?" + qs,
            auto_refresh_after=auto_refresh,
            auto_delay_ms=800,
        )

    def tap(self, name: str) -> None:
        self.raw("tap", MASK[name], auto_refresh=True)

    def down(self, name: str) -> None:
        if name in self.remote_held:
            self.remote_held[name] = True
            self._update_held_status()
        self.raw("down", MASK[name], auto_refresh=False)

    def up(self, name: str) -> None:
        if name in self.remote_held:
            self.remote_held[name] = False
            self._update_held_status()
        self.raw("up", MASK[name], auto_refresh=True)

    def press(self, name: str, ms: int) -> None:
        self.down(name)
        self.root.after(ms, lambda: self.up(name))

    def encoder(self, direction: str) -> None:
        qs = parse.urlencode({"d": direction})
        self.get_path(
            "/renc?" + qs,
            auto_refresh_after=True,
            auto_delay_ms=800,
        )

    def clear_remote(self) -> None:
        for key in self.remote_held:
            self.remote_held[key] = False
        self._update_held_status()
        self.get_path(
            "/rclr",
            auto_refresh_after=True,
            auto_delay_ms=800,
        )


def main() -> None:
    root = tk.Tk()
    app = MiniJV880RemoteGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()
