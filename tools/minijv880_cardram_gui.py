#!/usr/bin/env python3
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk
from pathlib import Path

import minijv880_cardram_tool as cardtool


class CardEntry:
    def __init__(self, path: Path, data: bytes):
        self.path = path
        self.data = data

    @property
    def label(self) -> str:
        return f"{self.path.name}  (0x{cardtool.fnv1a32(self.data):08X})"


class MiniJV880CardRamGui(tk.Tk):
    def __init__(self):
        super().__init__()

        self.title("MiniJV880 CardRAM Manager")
        self.geometry("1200x840")

        self.base_card = None
        self.source_cards = []

        self.working_data = None
        self.working_dirty = False
        self.working_change_count = 0

        self.performance_filter_var = tk.StringVar()
        self.patch_filter_var = tk.StringVar()

        self.last_slot_info_tree = None
        self.action_window_geometries = {}
        self.action_windows = {}

        self.reports_window = None
        self.reports_window_geometry = None

        self.restore_changed_slots_window = None
        self.restore_changed_slots_window_geometry = None

        self.text_window_geometry = None
        self.shortcuts_window = None
        self.shortcuts_window_geometry = None
        self.slot_dialog_geometries = {}
        self.colored_confirm_window_geometry = None

        self._build_ui()
        self.setup_keyboard_shortcuts()
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    def run_keyboard_shortcut(self, command):
        command()
        return "break"

    def run_global_keyboard_shortcut(self, event, command):
        try:
            if event.widget.winfo_toplevel() != self:
                return None

        except tk.TclError:
            return None

        return self.run_keyboard_shortcut(command)

    def focus_current_filter(self):
        current_tab = self.notebook.select()

        if current_tab == str(self.performance_frame):
            self.performance_filter_entry.focus_set()
            self.performance_filter_entry.select_range(0, tk.END)
            return

        if current_tab == str(self.patch_frame):
            self.patch_filter_entry.focus_set()
            self.patch_filter_entry.select_range(0, tk.END)
            return

    def clear_filter_shortcut(self, filter_var):
        filter_var.set("")
        return "break"

    def focus_changed_slots(self):
        self.changed_tree.focus_set()

        selected = self.changed_tree.selection()
        children = self.changed_tree.get_children("")

        if selected:
            self.changed_tree.focus(selected[0])
            self.changed_tree.see(selected[0])
            return

        if children:
            self.changed_tree.selection_set(children[0])
            self.changed_tree.focus(children[0])
            self.changed_tree.see(children[0])
            return

        self.log("Changed items focus: no changed items currently listed.")

    def build_shortcuts_text(self):
        return "\n".join(
            [
                "MiniJV880 CardRAM Manager shortcuts",
                "====================================",
                "",
                "Main window:",
                "  F1         Open this shortcuts window",
                "  Ctrl+O     Open base/destination card",
                "  Ctrl+L     Add/load source card(s)",
                "  Ctrl+S     Save working card",
                "  Ctrl+R     Open Reports",
                "  Ctrl+F     Focus filter field for the current tab",
                "  Ctrl+1     Select Performances tab",
                "  Ctrl+2     Select Patches tab",
                "  Ctrl+3     Focus Changed items list",
                "",
                "Filter fields:",
                "  Esc        Clear the focused filter field",
                "",
                "Slot lists:",
                "  Double-click    View selected slot info",
                "  Return          View selected slot info",
                "",
                "Changed items:",
                "  Double-click    Select changed item",
                "  Return          Select changed item",
                "",
                "Secondary windows:",
                "  Esc        Close non-destructive secondary windows",
                "",
                "Slot choice dialogs:",
                "  Return     Confirm selected slot",
                "  Esc        Cancel",
                "",
                "Notes:",
                "  Main-window shortcuts are active only while the main window has focus.",
                "  They do not act from secondary windows.",
                "  Shortcuts do not write files by themselves.",
                "  CardRAM .bin output is written only by Save working card.",
            ]
        )

    def show_shortcuts_window(self):
        if self.shortcuts_window is not None:
            try:
                if self.shortcuts_window.winfo_exists():
                    if self.shortcuts_window.state() == "withdrawn":
                        self.shortcuts_window.deiconify()

                    self.shortcuts_window.lift(self)
                    self.shortcuts_window.focus_set()
                    return

            except tk.TclError:
                pass

            self.shortcuts_window = None

        window = tk.Toplevel(self)
        window.title("MiniJV880 shortcuts")
        self.shortcuts_window = window

        window.geometry(self.shortcuts_window_geometry or "760x520")
        window.minsize(640, 420)
        window.transient(self)
        window.resizable(True, True)

        def remember_shortcuts_window_geometry():
            if not window.winfo_exists():
                return

            try:
                if window.state() == "normal":
                    self.shortcuts_window_geometry = window.geometry()

            except tk.TclError:
                pass

        def close_shortcuts_window():
            remember_shortcuts_window_geometry()
            self.shortcuts_window = None
            window.destroy()

        window.protocol("WM_DELETE_WINDOW", close_shortcuts_window)
        window.bind("<Escape>", lambda _event: close_shortcuts_window())

        window.bind(
            "<Configure>",
            lambda _event: remember_shortcuts_window_geometry(),
        )

        frame = ttk.Frame(window, padding=8)
        frame.pack(fill=tk.BOTH, expand=True)

        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        text = self.build_shortcuts_text()

        text_widget = tk.Text(frame, wrap=tk.NONE)
        text_widget.insert("1.0", text)
        text_widget.configure(state=tk.DISABLED)

        yscrollbar = ttk.Scrollbar(
            frame,
            orient=tk.VERTICAL,
            command=text_widget.yview,
        )
        xscrollbar = ttk.Scrollbar(
            frame,
            orient=tk.HORIZONTAL,
            command=text_widget.xview,
        )

        text_widget.configure(
            yscrollcommand=yscrollbar.set,
            xscrollcommand=xscrollbar.set,
        )

        text_widget.grid(row=0, column=0, sticky="nsew")
        yscrollbar.grid(row=0, column=1, sticky="ns")
        xscrollbar.grid(row=1, column=0, sticky="ew")

        actions = ttk.Frame(window, padding=(8, 0, 8, 8))
        actions.pack(fill=tk.X)

        ttk.Button(
            actions,
            text="Copy to clipboard",
            command=lambda: self.copy_text_to_clipboard(text),
        ).pack(side=tk.LEFT)

        ttk.Button(
            actions,
            text="Close",
            command=close_shortcuts_window,
        ).pack(side=tk.RIGHT)

    def setup_keyboard_shortcuts(self):
        self.bind_all(
            "<Control-o>",
            lambda event: self.run_global_keyboard_shortcut(event, self.open_base_card),
        )
        self.bind_all(
            "<Control-l>",
            lambda event: self.run_global_keyboard_shortcut(event, self.add_source_cards),
        )
        self.bind_all(
            "<Control-s>",
            lambda event: self.run_global_keyboard_shortcut(event, self.save_working_card),
        )
        self.bind_all(
            "<Control-r>",
            lambda event: self.run_global_keyboard_shortcut(event, self.open_reports_window),
        )
        self.bind_all(
            "<Control-f>",
            lambda event: self.run_global_keyboard_shortcut(event, self.focus_current_filter),
        )
        self.bind_all(
            "<F1>",
            lambda event: self.run_global_keyboard_shortcut(event, self.show_shortcuts_window),
        )
        self.bind_all(
            "<Control-Key-1>",
            lambda event: self.run_global_keyboard_shortcut(
                event,
                lambda: self.notebook.select(self.performance_frame),
            ),
        )
        self.bind_all(
            "<Control-Key-2>",
            lambda event: self.run_global_keyboard_shortcut(
                event,
                lambda: self.notebook.select(self.patch_frame),
            ),
        )
        self.bind_all(
            "<Control-Key-3>",
            lambda event: self.run_global_keyboard_shortcut(event, self.focus_changed_slots),
        )

    def _build_ui(self):
        self.performance_filter_var.trace_add(
            "write",
            lambda *_args: self.refresh_lists(),
        )
        self.patch_filter_var.trace_add(
            "write",
            lambda *_args: self.refresh_lists(),
        )    
        top = ttk.Frame(self, padding=8)
        top.pack(fill=tk.X)

        ttk.Button(top, text="Open base/destination card...", command=self.open_base_card).pack(side=tk.LEFT)
        ttk.Button(top, text="Add source card(s)...", command=self.add_source_cards).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="Save working card...", command=self.save_working_card).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="Reports...", command=self.open_reports_window).pack(side=tk.LEFT, padx=(8, 0))
        ttk.Button(top, text="Shortcuts...", command=self.show_shortcuts_window).pack(side=tk.LEFT, padx=(8, 0))

        self.base_label_var = tk.StringVar(value="Base: not loaded")
        ttk.Label(top, textvariable=self.base_label_var).pack(side=tk.LEFT, padx=(16, 0))

        source_frame = ttk.Frame(self, padding=(8, 0, 8, 8))
        source_frame.pack(fill=tk.X)

        ttk.Label(source_frame, text="Source card:").pack(side=tk.LEFT)

        self.source_combo_var = tk.StringVar()
        self.source_combo = ttk.Combobox(
            source_frame,
            textvariable=self.source_combo_var,
            state="readonly",
            width=70,
        )
        self.source_combo.pack(side=tk.LEFT, padx=(8, 0), fill=tk.X, expand=True)
        self.source_combo.bind("<<ComboboxSelected>>", lambda _event: self.refresh_lists())

        ttk.Button(
            source_frame,
            text="Remove selected source",
            command=self.remove_selected_source_card,
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            source_frame,
            text="Clear sources",
            command=self.clear_source_cards,
        ).pack(side=tk.LEFT, padx=(8, 0))

        self.notebook = ttk.Notebook(self)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=8, pady=(0, 8))

        self.performance_frame = ttk.Frame(self.notebook, padding=8)
        self.patch_frame = ttk.Frame(self.notebook, padding=8)

        self.notebook.add(self.performance_frame, text="Performances")
        self.notebook.add(self.patch_frame, text="Patches")

        self.source_perf_tree = self._make_slot_pair(
            self.performance_frame,
            "Source performances (read only)",
            "Working destination performances (RAM)",
        )[0]
        self.base_perf_tree = self._last_right_tree

        self.source_perf_tree.bind(
            "<<TreeviewSelect>>",
            lambda _event: self.remember_slot_info_tree(self.source_perf_tree),
        )
        self.base_perf_tree.bind(
            "<<TreeviewSelect>>",
            lambda _event: self.remember_slot_info_tree(self.base_perf_tree),
        )
        self.source_perf_tree.bind(
            "<Double-1>",
            lambda _event: self.view_selected_performance_info(),
        )
        self.source_perf_tree.bind(
            "<Return>",
            lambda _event: self.view_selected_performance_info(),
        )
        self.base_perf_tree.bind(
            "<Double-1>",
            lambda _event: self.view_selected_performance_info(),
        )
        self.base_perf_tree.bind(
            "<Return>",
            lambda _event: self.view_selected_performance_info(),
        )

        perf_filter_frame = ttk.Frame(self.performance_frame)
        perf_filter_frame.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(8, 0))

        ttk.Label(
            perf_filter_frame,
            text="Filter perf.:",
        ).pack(side=tk.LEFT)

        self.performance_filter_entry = ttk.Entry(
            perf_filter_frame,
            textvariable=self.performance_filter_var,
            width=32,
        )
        self.performance_filter_entry.pack(side=tk.LEFT, padx=(8, 0))
        self.performance_filter_entry.bind(
            "<Escape>",
            lambda _event: self.clear_filter_shortcut(self.performance_filter_var),
        )

        ttk.Button(
            perf_filter_frame,
            text="Clear",
            command=lambda: self.performance_filter_var.set(""),
        ).pack(side=tk.LEFT, padx=(8, 0))

        perf_actions = ttk.Frame(self.performance_frame)
        perf_actions.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(8, 0))

        ttk.Button(
            perf_actions,
            text="Copy...",
            command=self.open_performance_copy_window,
        ).pack(side=tk.LEFT)

        ttk.Button(
            perf_actions,
            text="Edit...",
            command=self.open_performance_edit_window,
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            perf_actions,
            text="Inspect...",
            command=self.open_performance_inspect_window,
        ).pack(side=tk.LEFT, padx=(8, 0))

        self.source_patch_tree = self._make_slot_pair(
            self.patch_frame,
            "Source patches (read only)",
            "Working destination patches (RAM)",
        )[0]
        self.base_patch_tree = self._last_right_tree

        self.source_patch_tree.bind(
            "<<TreeviewSelect>>",
            lambda _event: self.remember_slot_info_tree(self.source_patch_tree),
        )
        self.base_patch_tree.bind(
            "<<TreeviewSelect>>",
            lambda _event: self.remember_slot_info_tree(self.base_patch_tree),
        )
        self.source_patch_tree.bind(
            "<Double-1>",
            lambda _event: self.view_selected_patch_info(),
        )
        self.source_patch_tree.bind(
            "<Return>",
            lambda _event: self.view_selected_patch_info(),
        )
        self.base_patch_tree.bind(
            "<Double-1>",
            lambda _event: self.view_selected_patch_info(),
        )
        self.base_patch_tree.bind(
            "<Return>",
            lambda _event: self.view_selected_patch_info(),
        )

        patch_filter_frame = ttk.Frame(self.patch_frame)
        patch_filter_frame.grid(row=2, column=0, columnspan=2, sticky="ew", pady=(8, 0))

        ttk.Label(
            patch_filter_frame,
            text="Filter patch:",
        ).pack(side=tk.LEFT)

        self.patch_filter_entry = ttk.Entry(
            patch_filter_frame,
            textvariable=self.patch_filter_var,
            width=32,
        )
        self.patch_filter_entry.pack(side=tk.LEFT, padx=(8, 0))
        self.patch_filter_entry.bind(
            "<Escape>",
            lambda _event: self.clear_filter_shortcut(self.patch_filter_var),
        )

        ttk.Button(
            patch_filter_frame,
            text="Clear",
            command=lambda: self.patch_filter_var.set(""),
        ).pack(side=tk.LEFT, padx=(8, 0))

        patch_actions = ttk.Frame(self.patch_frame)
        patch_actions.grid(row=3, column=0, columnspan=2, sticky="ew", pady=(8, 0))

        ttk.Button(
            patch_actions,
            text="Copy...",
            command=self.open_patch_copy_window,
        ).pack(side=tk.LEFT)

        ttk.Button(
            patch_actions,
            text="Edit...",
            command=self.open_patch_edit_window,
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            patch_actions,
            text="Inspect...",
            command=self.open_patch_inspect_window,
        ).pack(side=tk.LEFT, padx=(8, 0))

        changed_frame = ttk.LabelFrame(self, text="Changed items", padding=8)
        changed_frame.pack(fill=tk.X, padx=8, pady=(0, 8))

        changed_tree_frame = ttk.Frame(changed_frame)
        changed_tree_frame.pack(fill=tk.X)
        changed_tree_frame.columnconfigure(0, weight=1)

        self.changed_tree = self._make_changed_tree(changed_tree_frame)

        changed_scrollbar = ttk.Scrollbar(
            changed_tree_frame,
            orient=tk.VERTICAL,
            command=self.changed_tree.yview,
        )
        self.changed_tree.configure(yscrollcommand=changed_scrollbar.set)

        self.changed_tree.grid(row=0, column=0, sticky="ew")
        changed_scrollbar.grid(row=0, column=1, sticky="ns")

        self.changed_tree.bind(
            "<Double-1>",
            lambda _event: self.select_changed_slot_in_destination(),
        )
        self.changed_tree.bind(
            "<Return>",
            lambda _event: self.select_changed_slot_in_destination(),
        )

        changed_actions = ttk.Frame(changed_frame)
        changed_actions.pack(fill=tk.X, pady=(8, 0))

        ttk.Button(
            changed_actions,
            text="Select changed item",
            command=self.select_changed_slot_in_destination,
        ).pack(side=tk.LEFT)

        ttk.Button(
            changed_actions,
            text="Restore selected from base",
            command=self.restore_selected_changed_slot_from_base,
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            changed_actions,
            text="Restore changed items...",
            command=self.restore_all_changed_slots_from_base,
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            changed_actions,
            text="Save change report...",
            command=self.save_change_report,
        ).pack(side=tk.LEFT, padx=(8, 0))

        log_frame = ttk.LabelFrame(self, text="Status", padding=8)
        log_frame.pack(fill=tk.BOTH, padx=8, pady=(0, 8))

        status_actions = ttk.Frame(log_frame)
        status_actions.pack(fill=tk.X, pady=(0, 8))

        ttk.Button(
            status_actions,
            text="Clear status",
            command=self.clear_status,
        ).pack(side=tk.LEFT)

        ttk.Button(
            status_actions,
            text="Copy status to clipboard",
            command=self.copy_status_to_clipboard,
        ).pack(side=tk.LEFT, padx=(8, 0))

        status_text_frame = ttk.Frame(log_frame)
        status_text_frame.pack(fill=tk.BOTH, expand=True)

        status_text_frame.columnconfigure(0, weight=1)
        status_text_frame.rowconfigure(0, weight=1)

        self.status_text = tk.Text(status_text_frame, height=10, wrap=tk.WORD)
        self.status_text.tag_configure("warning", foreground="red")
        self.status_text.configure(state=tk.DISABLED)

        status_scrollbar = ttk.Scrollbar(
            status_text_frame,
            orient=tk.VERTICAL,
            command=self.status_text.yview,
        )
        self.status_text.configure(yscrollcommand=status_scrollbar.set)

        self.status_text.grid(row=0, column=0, sticky="nsew")
        status_scrollbar.grid(row=0, column=1, sticky="ns")

        self.log("Ready. Load a base card and one or more source cards.")
        self.log("Use Shortcuts... to view keyboard shortcuts and double-click actions.")

    def _make_slot_pair(self, parent, left_title, right_title):
        parent.columnconfigure(0, weight=1)
        parent.columnconfigure(1, weight=1)
        parent.rowconfigure(1, weight=1)

        ttk.Label(parent, text=left_title).grid(row=0, column=0, sticky="w")
        ttk.Label(parent, text=right_title).grid(row=0, column=1, sticky="w", padx=(8, 0))

        left_frame = ttk.Frame(parent)
        right_frame = ttk.Frame(parent)

        left_frame.grid(row=1, column=0, sticky="nsew", pady=(4, 0))
        right_frame.grid(row=1, column=1, sticky="nsew", padx=(8, 0), pady=(4, 0))

        left_frame.columnconfigure(0, weight=1)
        left_frame.rowconfigure(0, weight=1)
        right_frame.columnconfigure(0, weight=1)
        right_frame.rowconfigure(0, weight=1)

        left_tree = self._make_slot_tree(left_frame)
        right_tree = self._make_slot_tree(right_frame)

        left_scrollbar = ttk.Scrollbar(
            left_frame,
            orient=tk.VERTICAL,
            command=left_tree.yview,
        )
        right_scrollbar = ttk.Scrollbar(
            right_frame,
            orient=tk.VERTICAL,
            command=right_tree.yview,
        )

        left_tree.configure(yscrollcommand=left_scrollbar.set)
        right_tree.configure(yscrollcommand=right_scrollbar.set)

        left_tree.grid(row=0, column=0, sticky="nsew")
        left_scrollbar.grid(row=0, column=1, sticky="ns")

        right_tree.grid(row=0, column=0, sticky="nsew")
        right_scrollbar.grid(row=0, column=1, sticky="ns")

        self._last_right_tree = right_tree
        return left_tree, right_tree

    def remember_slot_info_tree(self, tree):
        self.last_slot_info_tree = tree

    def _make_changed_tree(self, parent):
        tree = ttk.Treeview(
            parent,
            columns=("area", "slot", "base_name", "working_name", "base_digest", "working_digest"),
            show="headings",
            selectmode="browse",
            height=5,
        )

        tree.heading("area", text="Area")
        tree.heading("slot", text="#")
        tree.heading("base_name", text="Base name")
        tree.heading("working_name", text="Working name")
        tree.heading("base_digest", text="Base digest")
        tree.heading("working_digest", text="Working digest")

        tree.column("area", width=100, anchor=tk.W)
        tree.column("slot", width=50, anchor=tk.E)
        tree.column("base_name", width=180, anchor=tk.W)
        tree.column("working_name", width=180, anchor=tk.W)
        tree.column("base_digest", width=120, anchor=tk.CENTER)
        tree.column("working_digest", width=120, anchor=tk.CENTER)

        return tree

    def _make_slot_tree(self, parent):
        tree = ttk.Treeview(
            parent,
            columns=("slot", "name", "state", "offset", "digest"),
            show="headings",
            selectmode="browse",
        )

        tree.heading("slot", text="#")
        tree.heading("name", text="Name")
        tree.heading("state", text="State")
        tree.heading("offset", text="Offset")
        tree.heading("digest", text="Slot digest")

        tree.column("slot", width=50, anchor=tk.E)
        tree.column("name", width=160, anchor=tk.W)
        tree.column("state", width=70, anchor=tk.CENTER)
        tree.column("offset", width=90, anchor=tk.CENTER)
        tree.column("digest", width=110, anchor=tk.CENTER)

        tree.tag_configure("initial", foreground="gray")
        tree.tag_configure("changed", foreground="blue")

        return tree

    def rhythm_area_is_changed_from_base(self):
        if self.base_card is None or self.working_data is None:
            return False

        start = cardtool.RHYTHM_AREA_START
        end = start + cardtool.RHYTHM_AREA_SIZE

        base_rhythm_data = self.base_card.data[start:end]
        working_rhythm_data = self.working_data[start:end]

        if len(base_rhythm_data) != cardtool.RHYTHM_AREA_SIZE:
            return False

        if len(working_rhythm_data) != cardtool.RHYTHM_AREA_SIZE:
            return False

        return base_rhythm_data != working_rhythm_data

    def collect_changed_items(self):
        changes = list(self.collect_changed_slots())

        if self.base_card is None or self.working_data is None:
            return changes

        if self.rhythm_area_is_changed_from_base():
            start = cardtool.RHYTHM_AREA_START
            end = start + cardtool.RHYTHM_AREA_SIZE

            base_rhythm_data = self.base_card.data[start:end]
            working_rhythm_data = self.working_data[start:end]

            changes.append(
                (
                    "Rhythm",
                    1,
                    "raw block",
                    "raw block",
                    cardtool.fnv1a32(base_rhythm_data),
                    cardtool.fnv1a32(working_rhythm_data),
                )
            )

        return changes

    def unsaved_change_count(self):
        return len(self.collect_changed_items())

    def confirm_discard_unsaved_changes(self, action_text: str) -> bool:
        changed_count = self.unsaved_change_count()

        if changed_count == 0:
            return True

        return self.ask_colored_yesno(
            "Unsaved changes",
            [
                (
                    "The working destination has unsaved changes.\n\n"
                    f"Changed item(s): {changed_count}\n\n"
                    f"{action_text}\n\n"
                    "If you continue, unsaved RAM changes will be ",
                    None,
                ),
                ("lost", "red"),
                (
                    ".\n"
                    "No CardRAM .bin file will be written now.\n\n"
                    "Continue?",
                    None,
                ),
            ],
        )

    def on_close(self):
        if not self.confirm_discard_unsaved_changes("Close MiniJV880 CardRAM Manager?"):
            return

        self.destroy()

    def open_base_card(self):
        if not self.confirm_discard_unsaved_changes("Open another base/destination card?"):
            return

        filename = filedialog.askopenfilename(
            title="Open base/destination CardRAM .bin",
            filetypes=[("CardRAM BIN files", "*.bin"), ("All files", "*.*")],
        )

        if not filename:
            return

        try:
            path = Path(filename)
            data = cardtool.read_file(path)
            cardtool.require_card(path, data)
        except Exception as e:
            messagebox.showerror("Invalid base card", str(e))
            return

        self.base_card = CardEntry(path, data)
        self.working_data = bytes(data)
        self.working_dirty = False
        self.working_change_count = 0

        self.update_base_label()
        self.log(f"Loaded base card: {path}")
        self.log("Working destination initialized in memory.")
        self.refresh_lists()

    def refresh_source_combo(self, preferred_index=None):
        self.source_combo["values"] = [card.label for card in self.source_cards]

        if not self.source_cards:
            self.source_combo.set("")
            return

        if preferred_index is None:
            preferred_index = self.source_combo.current()

        if preferred_index < 0:
            preferred_index = 0

        if preferred_index >= len(self.source_cards):
            preferred_index = len(self.source_cards) - 1

        self.source_combo.current(preferred_index)

    def add_source_cards(self):
        filenames = filedialog.askopenfilenames(
            title="Add source CardRAM .bin file(s)",
            filetypes=[("CardRAM BIN files", "*.bin"), ("All files", "*.*")],
        )

        if not filenames:
            return

        loaded = 0
        skipped = 0

        for filename in filenames:
            try:
                path = Path(filename)
                data = cardtool.read_file(path)
                cardtool.require_card(path, data)
            except Exception as e:
                messagebox.showerror("Invalid source card", f"{filename}\n\n{e}")
                continue

            if any(card.path.resolve() == path.resolve() for card in self.source_cards):
                skipped += 1
                self.log(f"Source card already loaded, skipped: {path}")
                continue

            source_digest = cardtool.fnv1a32(data)

            if self.base_card is not None:
                if path.resolve() == self.base_card.path.resolve():
                    self.log(f"Warning: source card is the currently loaded base card: {path}")
                elif source_digest == cardtool.fnv1a32(self.base_card.data):
                    self.log(f"Warning: source card has the same digest as the loaded base card: {path}")

            for card in self.source_cards:
                if source_digest == cardtool.fnv1a32(card.data):
                    self.log(
                        "Warning: source card has the same digest as another loaded source card: "
                        f"{path} == {card.path}"
                    )
                    break

            self.source_cards.append(CardEntry(path, data))
            loaded += 1
            self.log(f"Loaded source card: {path}")

        if skipped > 0 and loaded == 0:
            self.log("No new source cards loaded.")

        if loaded > 0 and self.source_combo.current() < 0:
            self.refresh_source_combo(0)
        else:
            self.refresh_source_combo()

        self.refresh_lists()

    def remove_selected_source_card(self):
        index = self.source_combo.current()

        if index < 0 or index >= len(self.source_cards):
            messagebox.showerror("Remove source card", "Select one source card first.")
            return

        selected = self.source_cards[index]

        confirmed = messagebox.askyesno(
            "Remove selected source card?",
            "Remove this source card from the GUI?\n\n"
            f"Source card:\n{selected.path}\n\n"
            "The source .bin file will not be modified.\n"
            "The working destination will not be changed.\n"
            "No file will be written.\n\n"
            "Continue?",
        )

        if not confirmed:
            return

        removed = self.source_cards.pop(index)

        self.refresh_source_combo(index)
        self.refresh_lists()

        self.log(f"Removed source card: {removed.path}")

    def clear_source_cards(self):
        if not self.source_cards:
            self.log("Clear sources skipped: no source cards loaded.")
            return

        count = len(self.source_cards)

        confirmed = messagebox.askyesno(
            "Clear source cards?",
            "Remove all loaded source cards from the GUI?\n\n"
            f"Loaded source cards: {count}\n\n"
            "The source .bin files will not be modified.\n"
            "The working destination will not be changed.\n"
            "No file will be written.\n\n"
            "Continue?",
        )

        if not confirmed:
            return

        self.source_cards = []

        self.refresh_source_combo()
        self.refresh_lists()

        self.log(f"Cleared source cards: {count}")

    def selected_source_card(self):
        index = self.source_combo.current()

        if index < 0 or index >= len(self.source_cards):
            return None

        return self.source_cards[index]

    def working_slot_is_changed_from_base(self, area, slot):
        if self.base_card is None or self.working_data is None:
            return False

        if area == "performance":
            area_start = cardtool.PERFORMANCE_AREA_START
            slot_size = cardtool.PERFORMANCE_SLOT_SIZE
            slot_count = cardtool.PERFORMANCE_SLOT_COUNT

        elif area == "patch":
            area_start = cardtool.PATCH_AREA_START
            slot_size = cardtool.PATCH_SLOT_SIZE
            slot_count = cardtool.PATCH_SLOT_COUNT

        else:
            return False

        if slot < 1 or slot > slot_count:
            return False

        offset = area_start + (slot - 1) * slot_size

        base_slot_data = self.base_card.data[offset:offset + slot_size]
        working_slot_data = self.working_data[offset:offset + slot_size]

        if len(base_slot_data) != slot_size or len(working_slot_data) != slot_size:
            return False

        return base_slot_data != working_slot_data

    def slot_state_label(self, data, area, slot):
        try:
            if area == "performance":
                if cardtool.is_initial_performance_slot(data, slot):
                    return "INITIAL"

                return ""

            if area == "patch":
                if cardtool.is_initial_patch_slot(data, slot):
                    return "INITIAL"

                return ""

        except Exception:
            return "?"

        return ""

    def _selected_tree_slot(self, tree, label):
        selected = tree.selection()

        if not selected:
            raise ValueError(f"select one {label} slot first")

        return int(selected[0])

    def update_base_label(self):
        if self.base_card is None:
            self.base_label_var.set("Base: not loaded")
            return

        if self.working_data is None:
            self.base_label_var.set(f"Base: {self.base_card.label}")
            return

        changed_item_count = self.unsaved_change_count()
        self.working_dirty = changed_item_count > 0

        dirty_text = "modified in memory" if self.working_dirty else "unchanged"
        digest = cardtool.fnv1a32(self.working_data)

        self.base_label_var.set(
            f"Base: {self.base_card.path.name}  |  "
            f"Working: {dirty_text}, {changed_item_count} changed item(s), "
            f"digest 0x{digest:08X}"
        )

    def save_working_card(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Save working card", "Load a base/destination card first.")
            return

        check_text, check_ok = cardtool.render_check_card_report(
            self.base_card.path,
            self.working_data,
        )

        if not check_ok:
            self.log("")
            self.log("Save aborted: check-card returned WARN for the working destination.")
            self.log(check_text)
            messagebox.showerror(
                "Save working card",
                "The working destination did not pass check-card.\n\n"
                "No file was written.",
            )
            return

        filename = filedialog.asksaveasfilename(
            title="Save working CardRAM .bin",
            initialdir=str(self.base_card.path.parent),
            initialfile=f"{self.base_card.path.stem}_edited.bin",
            defaultextension=".bin",
            filetypes=[("CardRAM BIN files", "*.bin"), ("All files", "*.*")],
            confirmoverwrite=False,
        )

        if not filename:
            return

        output_path = Path(filename)

        try:
            output_parent = output_path.parent

            if str(output_parent) != "" and not output_parent.exists():
                raise ValueError(f"output directory does not exist: {output_parent}")

            if output_path.is_dir():
                raise ValueError(f"output path is a directory: {output_path}")

            overwrite_existing = output_path.exists()

            if overwrite_existing:
                if output_path.resolve() == self.base_card.path.resolve():
                    confirmed = self.ask_colored_yesno(
                        "Overwrite loaded base CardRAM?",
                        [
                            (
                                "This will overwrite the currently loaded base CardRAM file.\n\n"
                                f"File: {output_path}\n\n"
                                "The working card has already passed check-card.\n"
                                "The file will be written through a temporary .tmp file first.\n\n"
                                "If you continue, the loaded base file will be ",
                                None,
                            ),
                            ("overwritten", "red"),
                            (
                                ".\n\n"
                                "Continue?",
                                None,
                            ),
                        ],
                    )

                else:
                    confirmed = self.ask_colored_yesno(
                        "Overwrite existing CardRAM?",
                        [
                            (
                                "This will overwrite an existing CardRAM file.\n\n"
                                f"File: {output_path}\n\n"
                                "The working card has already passed check-card.\n"
                                "The file will be written through a temporary .tmp file first.\n\n"
                                "If you continue, the existing file will be ",
                                None,
                            ),
                            ("overwritten", "red"),
                            (
                                ".\n\n"
                                "Continue?",
                                None,
                            ),
                        ],
                    )

                if not confirmed:
                    return

                tmp_path = output_path.with_name(output_path.name + ".tmp")

                if tmp_path.exists():
                    raise ValueError(f"temporary file already exists: {tmp_path}")

                try:
                    tmp_path.write_bytes(self.working_data)
                    tmp_path.replace(output_path)
                except Exception:
                    tmp_path.unlink(missing_ok=True)
                    raise
            else:
                output_path.write_bytes(self.working_data)

        except Exception as e:
            messagebox.showerror("Save working card failed", str(e))
            return

        self.base_card = CardEntry(output_path, bytes(self.working_data))
        self.working_dirty = False

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Working card saved")
        self.log(f"  Output file:   {output_path}")
        self.log(f"  Output digest: 0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:    OK")
        self.log(f"  Overwrite:     {'yes' if overwrite_existing else 'no'}")

        messagebox.showinfo(
            "Save working card",
            "Working CardRAM saved and verified with check-card.\n\n"
            f"{output_path}",
        )

    def remember_action_window_geometry(self, key, window):
        if not window.winfo_exists():
            return

        try:
            if window.state() == "normal":
                self.action_window_geometries[key] = window.geometry()

        except tk.TclError:
            pass

    def run_action_from_action_window(self, window, command):
        before_windows = {
            child
            for child in self.winfo_children()
            if isinstance(child, tk.Toplevel) and child.winfo_exists()
        }

        action_window_key = window.title()
        saved_geometry = None

        if window.winfo_exists():
            try:
                window.update_idletasks()
                saved_geometry = window.geometry()
                self.action_window_geometries[action_window_key] = saved_geometry

                window.withdraw()
                window.update_idletasks()
            except tk.TclError:
                pass

        def execute_command():
            nonlocal saved_geometry

            try:
                command()
            finally:
                if not window.winfo_exists():
                    return

                try:
                    after_windows = {
                        child
                        for child in self.winfo_children()
                        if isinstance(child, tk.Toplevel) and child.winfo_exists()
                    }

                    new_windows = [
                        child
                        for child in after_windows - before_windows
                        if child != window and child.winfo_exists()
                    ]

                    if saved_geometry is None:
                        saved_geometry = self.action_window_geometries.get(
                            action_window_key,
                        )

                    if saved_geometry:
                        window.geometry(saved_geometry)

                    window.deiconify()
                    window.update_idletasks()

                    if saved_geometry:
                        window.geometry(saved_geometry)
                        self.action_window_geometries[action_window_key] = saved_geometry

                    window.lift(self)
                    window.focus_set()

                    for child in new_windows:
                        if child.winfo_exists() and child.winfo_viewable():
                            child.lift(window)

                except tk.TclError:
                    pass

        self.after_idle(execute_command)

    def open_action_window(self, title, description, actions):
        existing_window = self.action_windows.get(title)

        if existing_window is not None:
            try:
                if existing_window.winfo_exists():
                    if existing_window.state() == "withdrawn":
                        existing_window.deiconify()

                    existing_window.lift(self)
                    existing_window.focus_set()
                    return

            except tk.TclError:
                pass

            self.action_windows.pop(title, None)

        window = tk.Toplevel(self)
        window.title(title)
        self.action_windows[title] = window

        action_count = len(actions)
        default_height = 260 + max(0, action_count - 2) * 32
        min_height = 245 + max(0, action_count - 2) * 30

        if default_height > 370:
            default_height = 370

        if min_height > 345:
            min_height = 345

        default_geometry = f"520x{default_height}"

        window.geometry(self.action_window_geometries.get(title, default_geometry))
        window.minsize(480, min_height)
        window.transient(self)
        window.resizable(True, True)

        def close_window():
            self.remember_action_window_geometry(title, window)
            self.action_windows.pop(title, None)
            window.destroy()

        window.protocol("WM_DELETE_WINDOW", close_window)
        window.bind("<Escape>", lambda _event: close_window())

        window.bind(
            "<Configure>",
            lambda _event, key=title, window=window: self.remember_action_window_geometry(
                key,
                window,
            ),
        )

        frame = ttk.Frame(window, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            frame,
            text=title,
            font=("TkDefaultFont", 11, "bold"),
        ).pack(anchor=tk.W)

        ttk.Label(
            frame,
            text=description,
            wraplength=470,
        ).pack(anchor=tk.W, pady=(4, 12))

        actions_frame = ttk.Frame(frame)
        actions_frame.pack(fill=tk.X)

        for index, (button_text, command) in enumerate(actions):
            ttk.Button(
                actions_frame,
                text=button_text,
                command=lambda window=window, command=command: self.run_action_from_action_window(
                    window,
                    command,
                ),
            ).pack(anchor=tk.W, fill=tk.X, pady=(0 if index == 0 else 6, 0))

        ttk.Button(
            frame,
            text="Close",
            command=close_window,
        ).pack(anchor=tk.E, pady=(12, 0))

    def open_performance_copy_window(self):
        self.open_action_window(
            "Performance copy actions",
            "Copy Performance slots into the working destination. "
            "No file is written until Save working card.",
            [
                (
                    "Copy selected source perf. -> slot...",
                    self.copy_selected_performance,
                ),
                (
                    "Copy selected source perf. -> same slot...",
                    self.copy_selected_performance_to_same_slot,
                ),
                (
                    "Copy source perf. bank...",
                    self.copy_source_performance_bank,
                ),
                (
                    "Copy working perf. -> slot...",
                    self.copy_working_performance_to_slot,
                ),
            ],
        )

    def open_performance_edit_window(self):
        self.open_action_window(
            "Performance edit actions",
            "Edit, rearrange, or clear Performance slots. "
            "These actions apply only to slots selected in the Working destination performances (RAM) list. "
            "No source card is modified. "
            "No file is written until Save working card.",
            [
                (
                    "Rename selected perf...",
                    self.rename_selected_performance,
                ),
                (
                    "Swap selected perf...",
                    self.swap_selected_performance,
                ),
                (
                    "Move selected perf. -> slot...",
                    self.move_selected_performance_to_empty,
                ),
                (
                    "Clear selected perf.",
                    self.clear_selected_performance,
                ),
                (
                    "Restore selected perf. from base",
                    self.restore_selected_performance_from_base,
                ),
            ],
        )

    def open_performance_inspect_window(self):
        self.open_action_window(
            "Performance inspect actions",
            "View or compare Performance slot information. "
            "These actions do not modify the working destination.",
            [
                (
                    "View perf. info...",
                    self.view_selected_performance_info,
                ),
                (
                    "Compare source vs working perf...",
                    self.compare_selected_performance_slots,
                ),
            ],
        )

    def open_patch_copy_window(self):
        self.open_action_window(
            "Patch copy actions",
            "Copy Patch slots into the working destination. "
            "No file is written until Save working card.",
            [
                (
                    "Copy selected source patch -> slot...",
                    self.copy_selected_patch,
                ),
                (
                    "Copy selected source patch -> same slot...",
                    self.copy_selected_patch_to_same_slot,
                ),
                (
                    "Copy source patch bank...",
                    self.copy_source_patch_bank,
                ),
                (
                    "Copy working patch -> slot...",
                    self.copy_working_patch_to_slot,
                ),
            ],
        )

    def open_patch_edit_window(self):
        self.open_action_window(
            "Patch edit actions",
            "Edit, rearrange, or clear Patch slots. "
            "These actions apply only to slots selected in the Working destination patches (RAM) list. "
            "No source card is modified. "
            "No file is written until Save working card.",
            [
                (
                    "Rename selected patch...",
                    self.rename_selected_patch,
                ),
                (
                    "Swap selected patch...",
                    self.swap_selected_patch,
                ),
                (
                    "Move selected patch -> slot...",
                    self.move_selected_patch_to_empty,
                ),
                (
                    "Clear selected patch",
                    self.clear_selected_patch,
                ),
                (
                    "Restore selected patch from base",
                    self.restore_selected_patch_from_base,
                ),
            ],
        )

    def open_patch_inspect_window(self):
        self.open_action_window(
            "Patch inspect actions",
            "View or compare Patch slot information. "
            "These actions do not modify the working destination.",
            [
                (
                    "View patch info...",
                    self.view_selected_patch_info,
                ),
                (
                    "Compare source vs working patch...",
                    self.compare_selected_patch_slots,
                ),
            ],
        )

    def open_reports_window(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Reports", "Load a base/destination card first.")
            return

        if self.reports_window is not None:
            try:
                if self.reports_window.winfo_exists():
                    if self.reports_window.state() == "withdrawn":
                        self.reports_window.deiconify()

                    self.reports_window.lift(self)
                    self.reports_window.focus_set()
                    return

            except tk.TclError:
                pass

            self.reports_window = None

        window = tk.Toplevel(self)
        window.title("MiniJV880 working reports")
        self.reports_window = window

        window.geometry(self.reports_window_geometry or "560x620")
        window.minsize(520, 570)
        window.transient(self)
        window.resizable(True, True)

        def remember_reports_window_geometry():
            if not window.winfo_exists():
                return

            try:
                if window.state() == "normal":
                    self.reports_window_geometry = window.geometry()

            except tk.TclError:
                pass

        def close_reports_window():
            remember_reports_window_geometry()
            self.reports_window = None
            window.destroy()

        window.protocol("WM_DELETE_WINDOW", close_reports_window)
        window.bind("<Escape>", lambda _event: close_reports_window())

        window.bind(
            "<Configure>",
            lambda _event: remember_reports_window_geometry(),
        )

        frame = ttk.Frame(window, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            frame,
            text="Working reports",
            font=("TkDefaultFont", 11, "bold"),
        ).pack(anchor=tk.W)

        ttk.Label(
            frame,
            text="Reports describe the working card currently held in RAM.",
        ).pack(anchor=tk.W, pady=(4, 12))

        check_frame = ttk.LabelFrame(frame, text="Working check-card", padding=8)
        check_frame.pack(fill=tk.X)

        ttk.Label(
            check_frame,
            text="Validate and summarize the working CardRAM image.",
        ).grid(row=0, column=0, columnspan=2, sticky="w")

        ttk.Button(
            check_frame,
            text="View",
            command=self.view_working_check_card_report,
        ).grid(row=1, column=0, sticky="w", pady=(8, 0))

        ttk.Button(
            check_frame,
            text="Save...",
            command=self.save_working_check_card_report,
        ).grid(row=1, column=1, sticky="w", padx=(8, 0), pady=(8, 0))

        slot_frame = ttk.LabelFrame(frame, text="Working slot list", padding=8)
        slot_frame.pack(fill=tk.X, pady=(8, 0))

        ttk.Label(
            slot_frame,
            text="List all working Performance and Patch slots.",
        ).grid(row=0, column=0, columnspan=2, sticky="w")

        ttk.Button(
            slot_frame,
            text="View",
            command=self.view_working_slot_list_report,
        ).grid(row=1, column=0, sticky="w", pady=(8, 0))

        ttk.Button(
            slot_frame,
            text="Save...",
            command=self.save_working_slot_list_report,
        ).grid(row=1, column=1, sticky="w", padx=(8, 0), pady=(8, 0))

        rhythm_frame = ttk.LabelFrame(frame, text="Working Rhythm area", padding=8)
        rhythm_frame.pack(fill=tk.X, pady=(8, 0))

        ttk.Label(
            rhythm_frame,
            text="View raw Rhythm area offset, size and digest for the working card.",
        ).grid(row=0, column=0, columnspan=2, sticky="w")

        ttk.Button(
            rhythm_frame,
            text="View",
            command=self.view_working_rhythm_info_report,
        ).grid(row=1, column=0, sticky="w", pady=(8, 0))

        ttk.Button(
            rhythm_frame,
            text="Save...",
            command=self.save_working_rhythm_info_report,
        ).grid(row=1, column=1, sticky="w", padx=(8, 0), pady=(8, 0))

        rhythm_compare_frame = ttk.LabelFrame(
            frame,
            text="Source vs working Rhythm",
            padding=8,
        )
        rhythm_compare_frame.pack(fill=tk.X, pady=(8, 0))

        ttk.Label(
            rhythm_compare_frame,
            text="Compare the selected source card Rhythm area with the working card in RAM.",
        ).grid(row=0, column=0, columnspan=2, sticky="w")

        ttk.Button(
            rhythm_compare_frame,
            text="View",
            command=self.view_source_working_rhythm_compare_report,
        ).grid(row=1, column=0, sticky="w", pady=(8, 0))

        ttk.Button(
            rhythm_compare_frame,
            text="Copy source Rhythm -> working",
            command=self.copy_source_rhythm_to_working,
        ).grid(row=1, column=1, sticky="w", padx=(8, 0), pady=(8, 0))

        ttk.Button(
            frame,
            text="Close",
            command=close_reports_window,
        ).pack(anchor=tk.E, pady=(12, 0))

    def build_working_slot_list_text(self):
        if self.base_card is None or self.working_data is None:
            raise ValueError("Load a base/destination card first.")

        performance_text = cardtool.render_list_performances_report(
            self.base_card.path,
            self.working_data,
            cardtool.PERFORMANCE_SLOT_COUNT,
        )

        patch_text = cardtool.render_list_patches_report(
            self.base_card.path,
            self.working_data,
            cardtool.PATCH_SLOT_COUNT,
        )

        return (
            "GUI context: working destination in memory\n"
            "This report describes the working card currently held in RAM.\n\n"
            + performance_text.rstrip()
            + "\n\n"
            + patch_text
        )

    def view_working_slot_list_report(self):
        try:
            text = self.build_working_slot_list_text()

        except Exception as e:
            messagebox.showerror("View working slot list failed", str(e))
            return

        self.show_text_window("Working slot list", text)

        self.log("")
        self.log("Viewed working slot list")
        self.log(f"  Base file:         {self.base_card.path}")
        self.log(f"  Performance slots: {cardtool.PERFORMANCE_SLOT_COUNT}")
        self.log(f"  Patch slots:       {cardtool.PATCH_SLOT_COUNT}")
        self.log(f"  Working digest:    0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  File written:      no")

    def save_working_slot_list_report(self):
        try:
            text = self.build_working_slot_list_text()

        except Exception as e:
            messagebox.showerror("Save working slot list failed", str(e))
            return

        filename = filedialog.asksaveasfilename(
            title="Save working slot list report",
            initialdir=str(self.base_card.path.parent),
            initialfile=f"{self.base_card.path.stem}_slot_list.txt",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
            confirmoverwrite=True,
        )

        if not filename:
            return

        output_path = Path(filename)

        try:
            output_parent = output_path.parent

            if str(output_parent) != "" and not output_parent.exists():
                raise ValueError(f"output directory does not exist: {output_parent}")

            if output_path.is_dir():
                raise ValueError(f"output path is a directory: {output_path}")

            output_path.write_text(text, encoding="utf-8")

        except Exception as e:
            messagebox.showerror("Save working slot list failed", str(e))
            return

        self.log("")
        self.log("Saved working slot list")
        self.log(f"  Output file:       {output_path}")
        self.log(f"  Base file:         {self.base_card.path}")
        self.log(f"  Performance slots: {cardtool.PERFORMANCE_SLOT_COUNT}")
        self.log(f"  Patch slots:       {cardtool.PATCH_SLOT_COUNT}")
        self.log(f"  Working digest:    0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  Card file written: no")

        messagebox.showinfo(
            "Save working slot list",
            "Working slot list report saved.\n\n"
            f"{output_path}",
        )

    def build_working_rhythm_info_text(self):
        if self.base_card is None or self.working_data is None:
            raise ValueError("Load a base/destination card first.")

        rhythm_text = cardtool.render_rhythm_info_report(
            self.base_card.path,
            self.working_data,
        )

        return (
            "GUI context: working destination in memory\n"
            "This report describes the raw Rhythm area currently held in RAM.\n"
            "No CardRAM .bin file is written by this report operation.\n\n"
            + rhythm_text
        )

    def view_working_rhythm_info_report(self):
        try:
            text = self.build_working_rhythm_info_text()

        except Exception as e:
            messagebox.showerror("View working Rhythm info failed", str(e))
            return

        self.show_text_window("Working Rhythm area info", text)

        rhythm_data = self.working_data[
            cardtool.RHYTHM_AREA_START:
            cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE
        ]

        self.log("")
        self.log("Viewed working Rhythm area info")
        self.log(f"  Base file:      {self.base_card.path}")
        self.log(f"  Rhythm start:   0x{cardtool.RHYTHM_AREA_START:04X}")
        self.log(f"  Rhythm size:    {cardtool.RHYTHM_AREA_SIZE} bytes")
        self.log(f"  Rhythm digest:  0x{cardtool.fnv1a32(rhythm_data):08X}")
        self.log(f"  Working digest: 0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  File written:   no")

    def save_working_rhythm_info_report(self):
        try:
            text = self.build_working_rhythm_info_text()

        except Exception as e:
            messagebox.showerror("Save working Rhythm info failed", str(e))
            return

        filename = filedialog.asksaveasfilename(
            title="Save working Rhythm area info",
            initialdir=str(self.base_card.path.parent),
            initialfile=f"{self.base_card.path.stem}_rhythm_info.txt",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
            confirmoverwrite=True,
        )

        if not filename:
            return

        output_path = Path(filename)

        try:
            output_parent = output_path.parent

            if str(output_parent) != "" and not output_parent.exists():
                raise ValueError(f"output directory does not exist: {output_parent}")

            if output_path.is_dir():
                raise ValueError(f"output path is a directory: {output_path}")

            output_path.write_text(text, encoding="utf-8")

        except Exception as e:
            messagebox.showerror("Save working Rhythm info failed", str(e))
            return

        rhythm_data = self.working_data[
            cardtool.RHYTHM_AREA_START:
            cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE
        ]

        self.log("")
        self.log("Saved working Rhythm area info")
        self.log(f"  Output file:    {output_path}")
        self.log(f"  Base file:      {self.base_card.path}")
        self.log(f"  Rhythm digest:  0x{cardtool.fnv1a32(rhythm_data):08X}")
        self.log(f"  Working digest: 0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  Card file written: no")

        messagebox.showinfo(
            "Save working Rhythm info",
            "Working Rhythm area info report saved.\n\n"
            f"{output_path}",
        )

    def build_source_working_rhythm_compare_text(self):
        source = self.selected_source_card()

        if source is None:
            raise ValueError("Load and select a source card first.")

        if self.base_card is None or self.working_data is None:
            raise ValueError("Load a base/destination card first.")

        compare_text, equal = cardtool.render_compare_rhythm_report(
            source.path,
            self.base_card.path,
            source.data,
            self.working_data,
            64,
        )

        text = (
            "GUI context: selected source card vs working destination in memory\n"
            "The Right file path shown below is the loaded base/destination file name,\n"
            "but the compared Right data is the current working card held in RAM.\n"
            "No CardRAM .bin file is written by this report operation.\n\n"
            + compare_text
        )

        return text, equal, source

    def view_source_working_rhythm_compare_report(self):
        try:
            text, equal, source = self.build_source_working_rhythm_compare_text()

        except Exception as e:
            messagebox.showerror("View source vs working Rhythm failed", str(e))
            return

        self.show_text_window("Source vs working Rhythm compare", text)

        source_rhythm_data = source.data[
            cardtool.RHYTHM_AREA_START:
            cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE
        ]
        working_rhythm_data = self.working_data[
            cardtool.RHYTHM_AREA_START:
            cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE
        ]

        self.log("")
        self.log("Viewed source vs working Rhythm compare")
        self.log(f"  Source file:     {source.path}")
        self.log(f"  Base file:       {self.base_card.path}")
        self.log(f"  Source digest:   0x{cardtool.fnv1a32(source_rhythm_data):08X}")
        self.log(f"  Working digest:  0x{cardtool.fnv1a32(working_rhythm_data):08X}")
        self.log(f"  Rhythm equal:    {'yes' if equal else 'no'}")
        self.log("  File written:    no")

    def copy_source_rhythm_to_working(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Copy Rhythm", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy Rhythm", "Load a base/destination card first.")
            return

        source_rhythm_data = source.data[
            cardtool.RHYTHM_AREA_START:
            cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE
        ]
        working_rhythm_data = self.working_data[
            cardtool.RHYTHM_AREA_START:
            cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE
        ]

        source_digest = cardtool.fnv1a32(source_rhythm_data)
        old_working_digest = cardtool.fnv1a32(working_rhythm_data)

        if source_rhythm_data == working_rhythm_data:
            self.log("")
            self.log("Copy Rhythm skipped")
            self.log("  Reason:          source Rhythm already matches working destination")
            self.log(f"  Source file:     {source.path}")
            self.log(f"  Base file:       {self.base_card.path}")
            self.log(f"  Rhythm digest:   0x{source_digest:08X}")
            self.log("  File written:    no")
            return

        confirmed = self.ask_colored_yesno(
            "Copy source Rhythm to working?",
            [
                (
                    "This will replace the raw Rhythm area in the working destination.\n\n"
                    f"Source card:\n{source.path}\n\n"
                    f"Base/destination card:\n{self.base_card.path}\n\n"
                    f"Rhythm area: 0x{cardtool.RHYTHM_AREA_START:04X}"
                    f"..0x{cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE - 1:04X}"
                    f" ({cardtool.RHYTHM_AREA_SIZE} bytes)\n\n"
                    f"Old working Rhythm digest: 0x{old_working_digest:08X}\n"
                    f"New source Rhythm digest:  0x{source_digest:08X}\n\n"
                    "If you continue, the working Rhythm area in RAM will be ",
                    None,
                ),
                ("overwritten", "red"),
                (
                    ".\n"
                    "The source card is read only and will remain unchanged.\n"
                    "No CardRAM .bin file will be written now.\n\n"
                    "Continue?",
                    None,
                ),
            ],
        )

        if not confirmed:
            return

        try:
            output, source_offset, copied_rhythm_data, old_target_rhythm_data = cardtool.copy_rhythm_area(
                source.data,
                self.working_data,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy Rhythm failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Copy Rhythm aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy Rhythm failed",
                "The modified working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        if output == self.working_data:
            self.log("")
            self.log("Copy Rhythm skipped")
            self.log("  Reason:          output is identical to current working destination")
            self.log(f"  Source file:     {source.path}")
            self.log("  File written:    no")
            return

        new_working_rhythm_data = output[
            cardtool.RHYTHM_AREA_START:
            cardtool.RHYTHM_AREA_START + cardtool.RHYTHM_AREA_SIZE
        ]

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy Rhythm applied to working destination")
        self.log(f"  Source file:        {source.path}")
        self.log(f"  Base file:          {self.base_card.path}")
        self.log(f"  Source offset:      0x{source_offset:04X}")
        self.log(f"  Area size:          {cardtool.RHYTHM_AREA_SIZE} bytes")
        self.log(f"  Old Rhythm digest:  0x{cardtool.fnv1a32(old_target_rhythm_data):08X}")
        self.log(f"  New Rhythm digest:  0x{cardtool.fnv1a32(new_working_rhythm_data):08X}")
        self.log(f"  Working digest:     0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:         OK")
        self.log("  File written:       no")
        self.log("  Save required:      yes")

    def build_working_check_card_text(self):
        if self.base_card is None or self.working_data is None:
            raise ValueError("Load a base/destination card first.")

        check_text, check_ok = cardtool.render_check_card_report(
            self.base_card.path,
            self.working_data,
        )

        text = (
            "GUI context: working destination in memory\n"
            "This report describes the working card currently held in RAM.\n"
            "No CardRAM .bin file is written by this report operation.\n\n"
            + check_text
        )

        return text, check_ok

    def view_working_check_card_report(self):
        try:
            text, check_ok = self.build_working_check_card_text()

        except Exception as e:
            messagebox.showerror("View working check-card failed", str(e))
            return

        self.show_text_window("Working check-card report", text)

        self.log("")
        self.log("Viewed working check-card report")
        self.log(f"  Base file:      {self.base_card.path}")
        self.log(f"  Working digest: 0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log(f"  check-card:     {'OK' if check_ok else 'WARN'}")
        self.log("  File written:   no")

    def save_working_check_card_report(self):
        try:
            text, check_ok = self.build_working_check_card_text()

        except Exception as e:
            messagebox.showerror("Save working check-card failed", str(e))
            return

        filename = filedialog.asksaveasfilename(
            title="Save working check-card report",
            initialdir=str(self.base_card.path.parent),
            initialfile=f"{self.base_card.path.stem}_check_card.txt",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
            confirmoverwrite=True,
        )

        if not filename:
            return

        output_path = Path(filename)

        try:
            output_parent = output_path.parent

            if str(output_parent) != "" and not output_parent.exists():
                raise ValueError(f"output directory does not exist: {output_parent}")

            if output_path.is_dir():
                raise ValueError(f"output path is a directory: {output_path}")

            output_path.write_text(text, encoding="utf-8")

        except Exception as e:
            messagebox.showerror("Save working check-card failed", str(e))
            return

        self.log("")
        self.log("Saved working check-card report")
        self.log(f"  Output file:    {output_path}")
        self.log(f"  Base file:      {self.base_card.path}")
        self.log(f"  Working digest: 0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log(f"  check-card:     {'OK' if check_ok else 'WARN'}")
        self.log("  Card file written: no")

        messagebox.showinfo(
            "Save working check-card",
            "Working check-card report saved.\n\n"
            f"{output_path}",
        )

    def show_text_window(self, title, text):
        window = tk.Toplevel(self)
        window.title(title)
        window.geometry(self.text_window_geometry or "760x520")
        window.minsize(640, 420)
        window.transient(self)
        window.resizable(True, True)

        def remember_text_window_geometry():
            if not window.winfo_exists():
                return

            try:
                if window.state() == "normal":
                    self.text_window_geometry = window.geometry()

            except tk.TclError:
                pass

        def close_text_window():
            remember_text_window_geometry()
            window.destroy()

        window.protocol("WM_DELETE_WINDOW", close_text_window)
        window.bind("<Escape>", lambda _event: close_text_window())

        window.bind(
            "<Configure>",
            lambda _event: remember_text_window_geometry(),
        )

        frame = ttk.Frame(window, padding=8)
        frame.pack(fill=tk.BOTH, expand=True)

        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

        text_widget = tk.Text(frame, wrap=tk.NONE)
        text_widget.insert("1.0", text)
        text_widget.configure(state=tk.DISABLED)

        yscrollbar = ttk.Scrollbar(
            frame,
            orient=tk.VERTICAL,
            command=text_widget.yview,
        )
        xscrollbar = ttk.Scrollbar(
            frame,
            orient=tk.HORIZONTAL,
            command=text_widget.xview,
        )

        text_widget.configure(
            yscrollcommand=yscrollbar.set,
            xscrollcommand=xscrollbar.set,
        )

        text_widget.grid(row=0, column=0, sticky="nsew")
        yscrollbar.grid(row=0, column=1, sticky="ns")
        xscrollbar.grid(row=1, column=0, sticky="ew")

        actions = ttk.Frame(window, padding=(8, 0, 8, 8))
        actions.pack(fill=tk.X)

        ttk.Button(
            actions,
            text="Copy to clipboard",
            command=lambda: self.copy_text_to_clipboard(text),
        ).pack(side=tk.LEFT)

        ttk.Button(
            actions,
            text="Close",
            command=close_text_window,
        ).pack(side=tk.RIGHT)

    def copy_text_to_clipboard(self, text):
        self.clipboard_clear()
        self.clipboard_append(text)

        char_count = len(text)

        self.log(f"Text copied to clipboard: {char_count} character(s).")

    def selected_slot_info_target(self, source_tree, working_tree, source_label, working_label):
        focused = self.focus_get()

        if focused == source_tree:
            selected = source_tree.selection()

            if not selected:
                raise ValueError(f"select one {source_label} slot first")

            source = self.selected_source_card()

            if source is None:
                raise ValueError("load and select a source card first")

            return source.path, source.data, int(selected[0]), source_label

        if focused == working_tree:
            selected = working_tree.selection()

            if not selected:
                raise ValueError(f"select one {working_label} slot first")

            if self.base_card is None or self.working_data is None:
                raise ValueError("load a base/destination card first")

            return self.base_card.path, self.working_data, int(selected[0]), working_label

        source_selected = source_tree.selection()
        working_selected = working_tree.selection()

        if self.last_slot_info_tree == source_tree and source_selected:
            source = self.selected_source_card()

            if source is None:
                raise ValueError("load and select a source card first")

            return source.path, source.data, int(source_selected[0]), source_label

        if self.last_slot_info_tree == working_tree and working_selected:
            if self.base_card is None or self.working_data is None:
                raise ValueError("load a base/destination card first")

            return self.base_card.path, self.working_data, int(working_selected[0]), working_label

        if source_selected and not working_selected:
            source = self.selected_source_card()

            if source is None:
                raise ValueError("load and select a source card first")

            return source.path, source.data, int(source_selected[0]), source_label

        if working_selected and not source_selected:
            if self.base_card is None or self.working_data is None:
                raise ValueError("load a base/destination card first")

            return self.base_card.path, self.working_data, int(working_selected[0]), working_label

        raise ValueError(f"select one {source_label} or {working_label} slot first")

    def view_selected_performance_info(self):
        try:
            path, data, slot, label = self.selected_slot_info_target(
                self.source_perf_tree,
                self.base_perf_tree,
                "source Performance",
                "working Performance",
            )

            text = cardtool.render_slot_info_report(
                path,
                data,
                "performance",
                slot,
                64,
            )

        except Exception as e:
            messagebox.showerror("View Performance info", str(e))
            return

        if label.startswith("working"):
            text = (
                "GUI context: working destination in memory\n"
                "No file is written by this view.\n\n"
                + text
            )

        self.show_text_window(f"Performance slot info - {slot:03d}", text)

    def view_selected_patch_info(self):
        try:
            path, data, slot, label = self.selected_slot_info_target(
                self.source_patch_tree,
                self.base_patch_tree,
                "source Patch",
                "working Patch",
            )

            text = cardtool.render_slot_info_report(
                path,
                data,
                "patch",
                slot,
                64,
            )

        except Exception as e:
            messagebox.showerror("View Patch info", str(e))
            return

        if label.startswith("working"):
            text = (
                "GUI context: working destination in memory\n"
                "No file is written by this view.\n\n"
                + text
            )

        self.show_text_window(f"Patch slot info - {slot:03d}", text)

    def render_slot_compare_text(
        self,
        title,
        source_path,
        working_path,
        area,
        source_slot,
        working_slot,
        source_offset,
        working_offset,
        source_slot_data,
        working_slot_data,
        name_bytes,
    ):
        source_name = cardtool.slot_display_name(source_slot_data, name_bytes)
        working_name = cardtool.slot_display_name(working_slot_data, name_bytes)

        source_digest = cardtool.fnv1a32(source_slot_data)
        working_digest = cardtool.fnv1a32(working_slot_data)

        equal = source_slot_data == working_slot_data
        diff_bytes = cardtool.count_diff_bytes(source_slot_data, working_slot_data)
        diff_ranges = cardtool.find_diff_ranges(source_slot_data, working_slot_data)

        source_state = self.slot_state_label(
            self.selected_source_card().data,
            area,
            source_slot,
        )
        working_state = self.slot_state_label(
            self.working_data,
            area,
            working_slot,
        )

        if source_state == "":
            source_state = "occupied"

        if working_state == "":
            working_state = "occupied"

        lines = [
            title,
            "=" * len(title),
            "",
            "GUI context: source slot vs working destination slot",
            "No file is written by this view.",
            "",
            "Source slot:",
            f"  file:    {source_path}",
            f"  slot:    {source_slot:03d}",
            f"  offset:  0x{source_offset:04X}",
            f"  name:    {source_name}",
            f"  state:   {source_state}",
            f"  digest:  0x{source_digest:08X}",
            "",
            "Working slot:",
            f"  file:    {working_path}",
            f"  slot:    {working_slot:03d}",
            f"  offset:  0x{working_offset:04X}",
            f"  name:    {working_name}",
            f"  state:   {working_state}",
            f"  digest:  0x{working_digest:08X}",
            "",
            "Comparison:",
            f"  equal:             {'yes' if equal else 'no'}",
            f"  different bytes:   {diff_bytes}",
            f"  different ranges:  {len(diff_ranges)}",
            "",
        ]

        if diff_ranges:
            lines.append("First different ranges inside slot:")

            for start, end in diff_ranges[:12]:
                length = end - start + 1

                if start == end:
                    lines.append(f"  +0x{start:04X}          1 byte")
                else:
                    lines.append(f"  +0x{start:04X} - +0x{end:04X}   {length} bytes")

            if len(diff_ranges) > 12:
                lines.append("")
                lines.append(f"... truncated after 12 ranges of {len(diff_ranges)}")

            lines.append("")

        return "\n".join(lines)

    def compare_selected_performance_slots(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Compare Performance", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Compare Performance", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.source_perf_tree,
                "source Performance",
            )
            working_slot = self._selected_tree_slot(
                self.base_perf_tree,
                "working Performance",
            )

            source_offset, source_slot_data = cardtool.read_complete_slot(
                source.data,
                cardtool.PERFORMANCE_AREA_START,
                cardtool.PERFORMANCE_SLOT_SIZE,
                cardtool.PERFORMANCE_SLOT_COUNT,
                source_slot,
                "performance",
            )
            working_offset, working_slot_data = cardtool.read_complete_slot(
                self.working_data,
                cardtool.PERFORMANCE_AREA_START,
                cardtool.PERFORMANCE_SLOT_SIZE,
                cardtool.PERFORMANCE_SLOT_COUNT,
                working_slot,
                "performance",
            )

        except Exception as e:
            messagebox.showerror("Compare Performance", str(e))
            return

        text = self.render_slot_compare_text(
            "MiniJV880 CardRAM Performance slot compare",
            source.path,
            self.base_card.path,
            "performance",
            source_slot,
            working_slot,
            source_offset,
            working_offset,
            source_slot_data,
            working_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )

        self.show_text_window(
            f"Compare Performance {source_slot:03d} -> {working_slot:03d}",
            text,
        )

        self.log("")
        self.log("Compared source Performance with working Performance")
        self.log(f"  Source file:    {source.path}")
        self.log(f"  Source slot:    {source_slot:03d}")
        self.log(f"  Working file:   {self.base_card.path}")
        self.log(f"  Working slot:   {working_slot:03d}")
        self.log(f"  Equal:          {'yes' if source_slot_data == working_slot_data else 'no'}")
        self.log("  File written:   no")

    def compare_selected_patch_slots(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Compare Patch", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Compare Patch", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.source_patch_tree,
                "source Patch",
            )
            working_slot = self._selected_tree_slot(
                self.base_patch_tree,
                "working Patch",
            )

            source_offset, source_slot_data = cardtool.read_complete_slot(
                source.data,
                cardtool.PATCH_AREA_START,
                cardtool.PATCH_SLOT_SIZE,
                cardtool.PATCH_SLOT_COUNT,
                source_slot,
                "patch",
            )
            working_offset, working_slot_data = cardtool.read_complete_slot(
                self.working_data,
                cardtool.PATCH_AREA_START,
                cardtool.PATCH_SLOT_SIZE,
                cardtool.PATCH_SLOT_COUNT,
                working_slot,
                "patch",
            )

        except Exception as e:
            messagebox.showerror("Compare Patch", str(e))
            return

        text = self.render_slot_compare_text(
            "MiniJV880 CardRAM Patch slot compare",
            source.path,
            self.base_card.path,
            "patch",
            source_slot,
            working_slot,
            source_offset,
            working_offset,
            source_slot_data,
            working_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )

        self.show_text_window(
            f"Compare Patch {source_slot:03d} -> {working_slot:03d}",
            text,
        )

        self.log("")
        self.log("Compared source Patch with working Patch")
        self.log(f"  Source file:    {source.path}")
        self.log(f"  Source slot:    {source_slot:03d}")
        self.log(f"  Working file:   {self.base_card.path}")
        self.log(f"  Working slot:   {working_slot:03d}")
        self.log(f"  Equal:          {'yes' if source_slot_data == working_slot_data else 'no'}")
        self.log("  File written:   no")

    def copy_source_performance_bank(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Copy Performance bank", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy Performance bank", "Load a base/destination card first.")
            return

        try:
            working_performance_bank_is_empty = all(
                cardtool.is_initial_performance_slot(self.working_data, slot)
                for slot in range(1, cardtool.PERFORMANCE_SLOT_COUNT + 1)
            )

        except Exception as e:
            messagebox.showerror("Copy Performance bank failed", str(e))
            return

        if working_performance_bank_is_empty:
            confirmed = messagebox.askyesno(
                "Copy entire Performance bank?",
                "This will copy all 16 source Performance slots into the empty working Performance bank.\n\n"
                f"Source card:\n{source.path}\n\n"
                "No existing working Performance slot will be overwritten.\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            confirmed = self.ask_colored_yesno(
                "Copy entire Performance bank?",
                [
                    (
                        "This will replace all 16 Performance slots in the working destination.\n\n"
                        f"Source card:\n{source.path}\n\n"
                        "If you continue, the entire working Performance bank will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The source card is read only and will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            output, source_offset, source_performance_data, old_target_performance_data = cardtool.copy_performance_bank_area(
                source.data,
                self.working_data,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy Performance bank failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Copy Performance bank aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy Performance bank failed",
                "The modified working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        if output == self.working_data:
            self.log("")
            self.log("Copy Performance bank skipped")
            self.log("  Reason:        source Performance bank already matches working destination")
            self.log(f"  Source file:   {source.path}")
            self.log("  File written:  no")
            return

        performance_area_size = (
            cardtool.PERFORMANCE_SLOT_COUNT *
            cardtool.PERFORMANCE_SLOT_SIZE
        )

        new_target_performance_data = output[
            cardtool.PERFORMANCE_AREA_START:
            cardtool.PERFORMANCE_AREA_START + performance_area_size
        ]

        old_digest = cardtool.fnv1a32(old_target_performance_data)
        new_digest = cardtool.fnv1a32(new_target_performance_data)

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy Performance bank applied to working destination")
        self.log(f"  Source file:      {source.path}")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Source offset:    0x{source_offset:04X}")
        self.log(f"  Area size:        {performance_area_size} bytes")
        self.log(f"  Slots copied:     {cardtool.PERFORMANCE_SLOT_COUNT}")
        self.log(f"  Old bank digest:  0x{old_digest:08X}")
        self.log(f"  New bank digest:  0x{new_digest:08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def swap_selected_performance(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Swap Performance", "Load a base/destination card first.")
            return

        try:
            slot_a = self._selected_tree_slot(
                self.base_perf_tree,
                "working Performance",
            )
        except Exception as e:
            messagebox.showerror("Swap Performance", str(e))
            return

        try:
            slot_b = self.choose_working_slot_dialog(
                "Swap Performance slots",
                "Choose the working Performance slot to swap with.\n\n"
                f"Selected slot: {slot_a:03d}\n\n"
                "All other working Performance slots are shown.\n"
                "No data will be lost: the two slots will be exchanged.",
                "performance",
                exclude_slot=slot_a,
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Swap Performance", str(e))
            return

        if slot_b is None:
            return

        if slot_a == slot_b:
            messagebox.showerror(
                "Swap Performance",
                "Select a different Performance slot to swap with.",
            )
            return

        try:
            output, offset_a, offset_b, old_slot_a_data, old_slot_b_data = cardtool.swap_performance_slots(
                self.working_data,
                slot_a,
                slot_b,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Swap Performance failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Swap Performance aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Swap Performance failed",
                "The swapped working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        old_name_a = cardtool.decode_patch_name(
            old_slot_a_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )
        old_name_b = cardtool.decode_patch_name(
            old_slot_b_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )

        if old_name_a == "":
            old_name_a = "<blank>"

        if old_name_b == "":
            old_name_b = "<blank>"

        new_slot_a_data = output[
            offset_a:
            offset_a + cardtool.PERFORMANCE_SLOT_SIZE
        ]
        new_slot_b_data = output[
            offset_b:
            offset_b + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        new_name_a = cardtool.decode_patch_name(
            new_slot_a_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )
        new_name_b = cardtool.decode_patch_name(
            new_slot_b_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )

        if new_name_a == "":
            new_name_a = "<blank>"

        if new_name_b == "":
            new_name_b = "<blank>"

        if output == self.working_data:
            self.log("")
            self.log("Swap Performance skipped")
            self.log(f"  Slot A:        {slot_a:03d}  {old_name_a}")
            self.log(f"  Slot B:        {slot_b:03d}  {old_name_b}")
            self.log("  Reason:        slot data is identical")
            self.log("  File written:  no")
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Swap Performance applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot A:           {slot_a:03d}")
        self.log(f"  Offset A:         0x{offset_a:04X}")
        self.log(f"  Old name A:       {old_name_a}")
        self.log(f"  New name A:       {new_name_a}")
        self.log(f"  Old digest A:     0x{cardtool.fnv1a32(old_slot_a_data):08X}")
        self.log(f"  New digest A:     0x{cardtool.fnv1a32(new_slot_a_data):08X}")
        self.log("")
        self.log(f"  Slot B:           {slot_b:03d}")
        self.log(f"  Offset B:         0x{offset_b:04X}")
        self.log(f"  Old name B:       {old_name_b}")
        self.log(f"  New name B:       {new_name_b}")
        self.log(f"  Old digest B:     0x{cardtool.fnv1a32(old_slot_b_data):08X}")
        self.log(f"  New digest B:     0x{cardtool.fnv1a32(new_slot_b_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def ask_colored_yesno(self, title, parts, parent=None):
        result = {"yes": False}

        if parent is None:
            parent = self

        window = tk.Toplevel(parent)
        window.title(title)
        window.geometry(self.colored_confirm_window_geometry or "600x390")
        window.minsize(560, 360)
        window.transient(parent)
        window.resizable(True, True)

        def remember_colored_confirm_window_geometry():
            if not window.winfo_exists():
                return

            try:
                if window.state() == "normal":
                    self.colored_confirm_window_geometry = window.geometry()

            except tk.TclError:
                pass

        window.bind(
            "<Configure>",
            lambda _event: remember_colored_confirm_window_geometry(),
        )

        frame = ttk.Frame(window, padding=14)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            frame,
            text=title,
            font=("TkDefaultFont", 11, "bold"),
        ).pack(anchor=tk.W)

        body_frame = ttk.Frame(frame)
        body_frame.pack(fill=tk.BOTH, expand=True, pady=(10, 12))

        warning_label = ttk.Label(
            body_frame,
            text="Warning",
            font=("TkDefaultFont", 10, "bold"),
        )
        warning_label.pack(anchor=tk.W)

        text_widget = tk.Text(
            body_frame,
            wrap=tk.WORD,
            height=11,
            borderwidth=1,
            highlightthickness=0,
            relief=tk.GROOVE,
            padx=8,
            pady=8,
        )

        try:
            text_widget.configure(background=window.cget("background"))
        except tk.TclError:
            pass

        text_widget.tag_configure("red", foreground="red")

        for text, tag in parts:
            if tag:
                text_widget.insert(tk.END, text, tag)
            else:
                text_widget.insert(tk.END, text)

        text_widget.configure(state=tk.DISABLED)
        text_widget.pack(fill=tk.BOTH, expand=True, pady=(6, 0))

        actions = ttk.Frame(frame)
        actions.pack(fill=tk.X)

        def confirm():
            remember_colored_confirm_window_geometry()
            result["yes"] = True
            window.destroy()

        def cancel():
            remember_colored_confirm_window_geometry()
            window.destroy()

        no_button = ttk.Button(
            actions,
            text="No",
            command=cancel,
        )
        no_button.pack(side=tk.RIGHT)

        ttk.Button(
            actions,
            text="Yes",
            command=confirm,
        ).pack(side=tk.RIGHT, padx=(0, 8))

        window.protocol("WM_DELETE_WINDOW", cancel)
        window.bind("<Escape>", lambda _event: cancel())

        no_button.focus_set()

        try:
            window.lift(parent)
        except tk.TclError:
            pass

        window.grab_set()
        window.wait_window()

        return result["yes"]

    def choose_working_slot_dialog(
        self,
        title,
        message,
        area,
        initial_slot=None,
        exclude_slot=None,
        require_initial=False,
    ):
        if self.working_data is None:
            raise ValueError("load a base/destination card first")

        if area == "performance":
            area_name = "Performance"
            area_start = cardtool.PERFORMANCE_AREA_START
            slot_size = cardtool.PERFORMANCE_SLOT_SIZE
            name_bytes = cardtool.PERFORMANCE_NAME_BYTES
            slot_count = cardtool.PERFORMANCE_SLOT_COUNT

        elif area == "patch":
            area_name = "Patch"
            area_start = cardtool.PATCH_AREA_START
            slot_size = cardtool.PATCH_SLOT_SIZE
            name_bytes = cardtool.PATCH_NAME_BYTES
            slot_count = cardtool.PATCH_SLOT_COUNT

        else:
            raise ValueError(f"unknown area: {area}")

        result = {"slot": None}

        geometry_key = f"{title}:{area}"

        window = tk.Toplevel(self)
        window.title(title)
        window.geometry(self.slot_dialog_geometries.get(geometry_key, "640x560"))
        window.minsize(600, 500)
        window.transient(self)
        window.resizable(True, True)

        def remember_slot_dialog_geometry():
            if not window.winfo_exists():
                return

            try:
                if window.state() == "normal":
                    self.slot_dialog_geometries[geometry_key] = window.geometry()

            except tk.TclError:
                pass

        def close_slot_dialog():
            remember_slot_dialog_geometry()
            window.destroy()

        window.protocol("WM_DELETE_WINDOW", close_slot_dialog)
        window.bind("<Escape>", lambda _event: close_slot_dialog())

        window.bind(
            "<Configure>",
            lambda _event: remember_slot_dialog_geometry(),
        )

        frame = ttk.Frame(window, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            frame,
            text=title,
            font=("TkDefaultFont", 11, "bold"),
        ).pack(anchor=tk.W)

        ttk.Label(
            frame,
            text=message,
            wraplength=570,
        ).pack(anchor=tk.W, pady=(4, 12))

        tree_frame = ttk.Frame(frame)
        tree_frame.pack(fill=tk.BOTH, expand=True)

        tree_frame.columnconfigure(0, weight=1)
        tree_frame.rowconfigure(0, weight=1)

        tree = ttk.Treeview(
            tree_frame,
            columns=("slot", "name", "state", "offset", "digest"),
            show="headings",
            selectmode="browse",
            height=12,
        )

        tree.heading("slot", text="#")
        tree.heading("name", text="Name")
        tree.heading("state", text="State")
        tree.heading("offset", text="Offset")
        tree.heading("digest", text="Slot digest")

        tree.column("slot", width=50, anchor=tk.E)
        tree.column("name", width=190, anchor=tk.W)
        tree.column("state", width=120, anchor=tk.CENTER)
        tree.column("offset", width=90, anchor=tk.CENTER)
        tree.column("digest", width=120, anchor=tk.CENTER)

        tree.tag_configure("initial", foreground="gray")
        tree.tag_configure("changed", foreground="blue")

        scrollbar = ttk.Scrollbar(
            tree_frame,
            orient=tk.VERTICAL,
            command=tree.yview,
        )
        tree.configure(yscrollcommand=scrollbar.set)

        tree.grid(row=0, column=0, sticky="nsew")
        scrollbar.grid(row=0, column=1, sticky="ns")

        rows = cardtool.collect_named_slots(
            self.working_data,
            area_name,
            area_start,
            slot_size,
            name_bytes,
            slot_count,
        )

        for _area_name, slot, offset, name, digest in rows:
            if exclude_slot is not None and slot == exclude_slot:
                continue

            state = self.slot_state_label(self.working_data, area, slot)
            changed = self.working_slot_is_changed_from_base(area, slot)

            if changed:
                if state == "INITIAL":
                    state = "INITIAL+CHANGED"
                else:
                    state = "CHANGED"

            if require_initial and not state.startswith("INITIAL"):
                continue

            tags = ()

            if changed:
                tags = ("changed",)
            elif state == "INITIAL":
                tags = ("initial",)

            tree.insert(
                "",
                tk.END,
                iid=str(slot),
                values=(
                    f"{slot:03d}",
                    name,
                    state,
                    f"0x{offset:04X}",
                    f"0x{digest:08X}",
                ),
                tags=tags,
            )

        children = tree.get_children()

        if initial_slot is not None and str(initial_slot) in children:
            tree.selection_set(str(initial_slot))
            tree.see(str(initial_slot))
            tree.focus(str(initial_slot))
        elif children:
            tree.selection_set(children[0])
            tree.see(children[0])
            tree.focus(children[0])

        tree.focus_set()

        actions = ttk.Frame(frame)
        actions.pack(fill=tk.X, pady=(12, 0))

        def confirm_selection():
            selected = tree.selection()

            if not selected:
                messagebox.showerror(
                    title,
                    "Select one working slot first.",
                    parent=window,
                )
                return

            result["slot"] = int(selected[0])
            close_slot_dialog()

        tree.bind("<Double-1>", lambda _event: confirm_selection())
        tree.bind("<Return>", lambda _event: confirm_selection())

        ttk.Button(
            actions,
            text="Select",
            command=confirm_selection,
        ).pack(side=tk.LEFT)

        ttk.Button(
            actions,
            text="Cancel",
            command=close_slot_dialog,
        ).pack(side=tk.RIGHT)

        window.grab_set()
        window.wait_window()

        return result["slot"]

    def move_selected_performance_to_empty(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Move Performance", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.base_perf_tree,
                "working Performance",
            )
        except Exception as e:
            messagebox.showerror("Move Performance", str(e))
            return

        try:
            if cardtool.is_initial_performance_slot(self.working_data, source_slot):
                messagebox.showerror(
                    "Move Performance",
                    "The selected working Performance slot is already INITIAL DATA.",
                )
                return

        except Exception as e:
            messagebox.showerror("Move Performance failed", str(e))
            return

        try:
            target_slot = self.choose_working_slot_dialog(
                "Move Performance to slot",
                "Choose the working Performance slot to move into.\n\n"
                f"Selected slot: {source_slot:03d}\n\n"
                "INITIAL DATA working slots will be used as empty slots.\n"
                "Occupied working slots will require an overwrite confirmation.",
                "performance",
                exclude_slot=source_slot,
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Move Performance", str(e))
            return

        if target_slot is None:
            return

        if source_slot == target_slot:
            messagebox.showerror(
                "Move Performance",
                "Selected and chosen Performance slots must be different.",
            )
            return

        source_offset = (
            cardtool.PERFORMANCE_AREA_START +
            (source_slot - 1) * cardtool.PERFORMANCE_SLOT_SIZE
        )
        target_offset = (
            cardtool.PERFORMANCE_AREA_START +
            (target_slot - 1) * cardtool.PERFORMANCE_SLOT_SIZE
        )

        source_slot_data = self.working_data[
            source_offset:
            source_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]
        old_target_slot_data = self.working_data[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        try:
            target_is_initial = cardtool.is_initial_performance_slot(
                self.working_data,
                target_slot,
            )

        except Exception as e:
            messagebox.showerror("Move Performance failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Move selected Performance?",
                "This will move the selected working Performance slot to the selected empty working slot.\n\n"
                f"Selected slot: {source_slot:03d}\n"
                f"Working slot: {target_slot:03d}\n\n"
                "The selected slot will become INITIAL DATA.\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

            if not confirmed:
                return

            try:
                (
                    output,
                    source_offset,
                    target_offset,
                    source_slot_data,
                    old_target_slot_data,
                    template_slot,
                    template_offset,
                    template_slot_data,
                ) = cardtool.move_performance_slot_to_empty(
                    self.working_data,
                    source_slot,
                    target_slot,
                )

                check_text, check_ok = cardtool.render_check_card_report(
                    self.base_card.path,
                    output,
                )

            except Exception as e:
                messagebox.showerror("Move Performance failed", str(e))
                return

            move_mode = "empty working slot"

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data,
                cardtool.PERFORMANCE_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Performance?",
                [
                    (
                        "The selected working Performance slot is occupied.\n\n"
                        f"Source slot: {source_slot:03d} ({source_state})\n"
                        f"Working slot: {target_slot:03d} ({target_state})\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Performance will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The source card is read only and will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

            if not confirmed:
                return

            try:
                output_buffer = bytearray(self.working_data)
                output_buffer[
                    target_offset:
                    target_offset + cardtool.PERFORMANCE_SLOT_SIZE
                ] = source_slot_data

                (
                    output,
                    cleared_source_offset,
                    old_source_after_target_write_data,
                    template_slot,
                    template_offset,
                    template_slot_data,
                ) = cardtool.clear_performance_slot(
                    bytes(output_buffer),
                    source_slot,
                )

                if cleared_source_offset != source_offset:
                    raise ValueError("internal error: cleared source offset mismatch")

                check_text, check_ok = cardtool.render_check_card_report(
                    self.base_card.path,
                    output,
                )

            except Exception as e:
                messagebox.showerror("Move Performance failed", str(e))
                return

            move_mode = "overwrite working slot"

        if not check_ok:
            self.log("")
            self.log("Move Performance aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Move Performance failed",
                "The moved working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        new_source_slot_data = output[
            source_offset:
            source_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]
        new_target_slot_data = output[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        old_source_name = cardtool.slot_display_name(
            source_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )
        new_source_name = cardtool.slot_display_name(
            new_source_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )
        old_target_name = cardtool.slot_display_name(
            old_target_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )
        new_target_name = cardtool.slot_display_name(
            new_target_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )
        template_name = cardtool.slot_display_name(
            template_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )

        if output == self.working_data:
            self.log("")
            self.log("Move Performance skipped")
            self.log(f"  Selected slot: {source_slot:03d}")
            self.log(f"  Working slot:  {target_slot:03d}")
            self.log("  Reason:        output is unchanged")
            self.log("  File written:  no")
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Move Performance applied to working destination")
        self.log(f"  Mode:              {move_mode}")
        self.log(f"  Base file:         {self.base_card.path}")
        self.log(f"  Source slot:       {source_slot:03d}")
        self.log(f"  Source offset:     0x{source_offset:04X}")
        self.log(f"  Old source name:   {old_source_name}")
        self.log(f"  New source name:   {new_source_name}")
        self.log(f"  Old source digest: 0x{cardtool.fnv1a32(source_slot_data):08X}")
        self.log(f"  New source digest: 0x{cardtool.fnv1a32(new_source_slot_data):08X}")
        self.log("")
        self.log(f"  Working slot:      {target_slot:03d}")
        self.log(f"  Working offset:    0x{target_offset:04X}")
        self.log(f"  Old working name:  {old_target_name}")
        self.log(f"  New working name:  {new_target_name}")
        self.log(f"  Old working digest: 0x{cardtool.fnv1a32(old_target_slot_data):08X}")
        self.log(f"  New working digest: 0x{cardtool.fnv1a32(new_target_slot_data):08X}")
        self.log("")
        self.log(f"  Template slot:     {template_slot:03d}")
        self.log(f"  Template offset:   0x{template_offset:04X}")
        self.log(f"  Template name:     {template_name}")
        self.log(f"  Template digest:   0x{cardtool.fnv1a32(template_slot_data):08X}")
        self.log(f"  Working digest:    0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:        OK")
        self.log("  File written:      no")

    def clear_selected_performance(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Clear Performance", "Load a base/destination card first.")
            return

        try:
            target_slot = self._selected_tree_slot(
                self.base_perf_tree,
                "working Performance",
            )
        except Exception as e:
            messagebox.showerror("Clear Performance", str(e))
            return

        try:
            target_is_initial = cardtool.is_initial_performance_slot(
                self.working_data,
                target_slot,
            )

            target_offset_for_warning = (
                cardtool.PERFORMANCE_AREA_START +
                (target_slot - 1) * cardtool.PERFORMANCE_SLOT_SIZE
            )

            old_target_slot_data_for_warning = self.working_data[
                target_offset_for_warning:
                target_offset_for_warning + cardtool.PERFORMANCE_SLOT_SIZE
            ]

        except Exception as e:
            messagebox.showerror("Clear Performance failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Clear selected Performance?",
                "The selected working Performance slot already contains INITIAL DATA.\n\n"
                f"Slot: {target_slot:03d}\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data_for_warning,
                cardtool.PERFORMANCE_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Clear selected Performance?",
                [
                    (
                        "This will replace the selected working Performance slot "
                        "with the card's INITIAL DATA Performance template.\n\n"
                        f"Slot: {target_slot:03d}\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the selected Performance slot will be ",
                        None,
                    ),
                    ("cleared", "red"),
                    (
                        ".\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            (
                output,
                target_offset,
                old_target_slot_data,
                template_slot,
                template_offset,
                template_slot_data,
            ) = cardtool.clear_performance_slot(
                self.working_data,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Clear Performance failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Clear Performance aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Clear Performance failed",
                "The cleared working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        old_name = cardtool.slot_display_name(
            old_target_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )
        template_name = cardtool.slot_display_name(
            template_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )

        new_target_slot_data = output[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]
        new_name = cardtool.slot_display_name(
            new_target_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )

        if output == self.working_data:
            self.log("")
            self.log("Clear Performance skipped")
            self.log(f"  Slot:          {target_slot:03d}")
            self.log("  Reason:        slot already matches INITIAL DATA template")
            self.log(f"  Name:          {new_name}")
            self.log("  File written:  no")
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Clear Performance applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {target_slot:03d}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old name:         {old_name}")
        self.log(f"  New name:         {new_name}")
        self.log(f"  Old digest:       0x{cardtool.fnv1a32(old_target_slot_data):08X}")
        self.log(f"  New digest:       0x{cardtool.fnv1a32(new_target_slot_data):08X}")
        self.log("")
        self.log(f"  Template slot:    {template_slot:03d}")
        self.log(f"  Template offset:  0x{template_offset:04X}")
        self.log(f"  Template name:    {template_name}")
        self.log(f"  Template digest:  0x{cardtool.fnv1a32(template_slot_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def rename_selected_performance(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Rename Performance", "Load a base/destination card first.")
            return

        try:
            target_slot = self._selected_tree_slot(
                self.base_perf_tree,
                "working Performance",
            )
        except Exception as e:
            messagebox.showerror("Rename Performance", str(e))
            return

        target_offset = (
            cardtool.PERFORMANCE_AREA_START +
            (target_slot - 1) * cardtool.PERFORMANCE_SLOT_SIZE
        )

        old_slot_data = self.working_data[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        old_name = cardtool.decode_patch_name(
            old_slot_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )

        if old_name == "":
            old_name = "<blank>"

        initial_name = "" if old_name == "<blank>" else old_name.rstrip()

        new_name = simpledialog.askstring(
            "Rename Performance",
            "New Performance name\n"
            f"Slot: {target_slot:03d}\n\n"
            "Maximum 12 printable ASCII characters.",
            initialvalue=initial_name,
            parent=self,
        )

        if new_name is None:
            return

        new_name = new_name.strip()

        try:
            output, name_offset, old_name_data, new_name_data = cardtool.set_performance_name(
                self.working_data,
                target_slot,
                new_name,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Rename Performance failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Rename Performance aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Rename Performance failed",
                "The renamed working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        if output == self.working_data:
            self.log("")
            self.log("Rename Performance skipped")
            self.log(f"  Slot:          {target_slot:03d}")
            self.log("  Reason:        name is unchanged")
            self.log(f"  Name:          {old_name}")
            self.log("  File written:  no")
            return

        new_slot_data = output[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        decoded_old_name = cardtool.decode_patch_name(old_name_data)
        decoded_new_name = cardtool.decode_patch_name(new_name_data)

        if decoded_old_name == "":
            decoded_old_name = "<blank>"

        if decoded_new_name == "":
            decoded_new_name = "<blank>"

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Rename Performance applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {target_slot:03d}")
        self.log(f"  Name offset:      0x{name_offset:04X}")
        self.log(f"  Old name:         {decoded_old_name}")
        self.log(f"  New name:         {decoded_new_name}")
        self.log(f"  Old slot digest:  0x{cardtool.fnv1a32(old_slot_data):08X}")
        self.log(f"  New slot digest:  0x{cardtool.fnv1a32(new_slot_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def copy_working_performance_to_slot(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy working Performance", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.base_perf_tree,
                "working Performance",
            )
        except Exception as e:
            messagebox.showerror("Copy working Performance", str(e))
            return

        try:
            target_slot = self.choose_working_slot_dialog(
                "Copy working Performance to slot",
                "Choose the working Performance slot to copy into.\n\n"
                f"Selected slot: {source_slot:03d}\n\n"
                "All other working Performance slots are shown.\n"
                "The chosen working slot will be replaced in RAM after confirmation.",
                "performance",
                exclude_slot=source_slot,
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Copy working Performance", str(e))
            return

        if target_slot is None:
            return

        if source_slot == target_slot:
            messagebox.showerror(
                "Copy working Performance",
                "Selected and chosen Performance slots must be different.",
            )
            return

        try:
            source_is_initial = cardtool.is_initial_performance_slot(
                self.working_data,
                source_slot,
            )
            target_is_initial = cardtool.is_initial_performance_slot(
                self.working_data,
                target_slot,
            )

            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_performance_slot(
                self.working_data,
                self.working_data,
                source_slot,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy working Performance failed", str(e))
            return

        source_state = "INITIAL" if source_is_initial else "occupied"
        target_state = "INITIAL" if target_is_initial else "occupied"

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Copy working Performance?",
                "This will copy one working Performance slot to an empty working slot.\n\n"
                f"Selected slot: {source_slot:03d} ({source_state})\n"
                f"Working slot: {target_slot:03d} ({target_state})\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data,
                cardtool.PERFORMANCE_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Performance?",
                [
                    (
                        "The chosen working Performance slot is occupied.\n\n"
                        f"Selected slot: {source_slot:03d} ({source_state})\n"
                        f"Working slot: {target_slot:03d} ({target_state})\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Performance will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The selected slot will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        if not check_ok:
            self.log("")
            self.log("Copy working Performance aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy working Performance failed",
                "The copied working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        source_name = cardtool.slot_display_name(
            source_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )
        old_target_name = cardtool.slot_display_name(
            old_target_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )

        if output == self.working_data:
            self.log("")
            self.log("Copy working Performance skipped")
            self.log(f"  Selected slot: {source_slot:03d}  {source_name}")
            self.log(f"  Working slot:  {target_slot:03d}  {old_target_name}")
            self.log("  Reason:        selected slot already matches working slot")
            self.log("  File written:  no")
            return

        new_target_slot_data = output[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]
        new_target_name = cardtool.slot_display_name(
            new_target_slot_data,
            cardtool.PERFORMANCE_NAME_BYTES,
        )

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy working Performance applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Selected slot:    {source_slot:03d}")
        self.log(f"  Selected offset:  0x{source_offset:04X}")
        self.log(f"  Selected name:    {source_name}")
        self.log(f"  Selected state:   {source_state}")
        self.log(f"  Selected digest:  0x{cardtool.fnv1a32(source_slot_data):08X}")
        self.log("")
        self.log(f"  Working slot:     {target_slot:03d}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_name}")
        self.log(f"  New working name: {new_target_name}")
        self.log(f"  Old working state: {target_state}")
        self.log(f"  Old working digest: 0x{cardtool.fnv1a32(old_target_slot_data):08X}")
        self.log(f"  New working digest: 0x{cardtool.fnv1a32(new_target_slot_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def copy_selected_performance_to_same_slot(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Copy Performance", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy Performance", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.source_perf_tree,
                "source Performance",
            )

        except Exception as e:
            messagebox.showerror("Copy Performance", str(e))
            return

        try:
            source_state = self.slot_state_label(source.data, "performance", source_slot)
            target_state = self.slot_state_label(self.working_data, "performance", source_slot)

            if source_state == "":
                source_state = "occupied"

            if target_state == "":
                target_state = "occupied"

            target_offset_for_warning = (
                cardtool.PERFORMANCE_AREA_START +
                (source_slot - 1) * cardtool.PERFORMANCE_SLOT_SIZE
            )

            old_target_slot_data_for_warning = self.working_data[
                target_offset_for_warning:
                target_offset_for_warning + cardtool.PERFORMANCE_SLOT_SIZE
            ]

            target_is_initial = cardtool.is_initial_performance_slot(
                self.working_data,
                source_slot,
            )

        except Exception as e:
            messagebox.showerror("Copy Performance failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Copy source Performance to same slot?",
                "This will copy the selected source Performance to the same empty working slot.\n\n"
                f"Slot: {source_slot:03d}\n"
                f"Source state: {source_state}\n"
                f"Working state: {target_state}\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data_for_warning,
                cardtool.PERFORMANCE_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Performance?",
                [
                    (
                        "The corresponding working Performance slot is occupied.\n\n"
                        f"Slot: {source_slot:03d}\n"
                        f"Source state: {source_state}\n"
                        f"Working state: {target_state}\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Performance will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The source card is read only and will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_performance_slot(
                source.data,
                self.working_data,
                source_slot,
                source_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy Performance failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Copy Performance same slot aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy Performance failed",
                "The modified working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        source_slot_name = cardtool.decode_patch_name(
            source_slot_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )
        old_target_slot_name = cardtool.decode_patch_name(
            old_target_slot_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )

        if source_slot_name == "":
            source_slot_name = "<blank>"

        if old_target_slot_name == "":
            old_target_slot_name = "<blank>"

        if output == self.working_data:
            self.log("")
            self.log("Copy Performance same slot skipped")
            self.log(f"  Slot:          {source_slot:03d}")
            self.log("  Reason:        source slot already matches working destination")
            self.log(f"  Name:          {source_slot_name}")
            self.log("  File written:  no")
            return

        new_target_data = output[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        new_target_slot_name = cardtool.decode_patch_name(
            new_target_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )

        if new_target_slot_name == "":
            new_target_slot_name = "<blank>"

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy Performance same slot applied to working destination")
        self.log(f"  Source file:      {source.path}")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {source_slot:03d}")
        self.log(f"  Source offset:    0x{source_offset:04X}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_slot_name}")
        self.log(f"  New working name: {new_target_slot_name}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def copy_selected_performance(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Copy Performance", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy Performance", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.source_perf_tree,
                "source Performance",
            )

        except Exception as e:
            messagebox.showerror("Copy Performance", str(e))
            return

        try:
            target_slot = self.choose_working_slot_dialog(
                "Copy source Performance to slot",
                "Choose the working Performance slot to replace.\n\n"
                f"Selected source slot: {source_slot:03d}\n\n"
                "The selected working slot will be replaced in RAM after confirmation.",
                "performance",
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Copy Performance", str(e))
            return

        if target_slot is None:
            return

        try:
            source_state = self.slot_state_label(source.data, "performance", source_slot)
            target_state = self.slot_state_label(self.working_data, "performance", target_slot)

            if source_state == "":
                source_state = "occupied"

            if target_state == "":
                target_state = "occupied"

            target_offset_for_warning = (
                cardtool.PERFORMANCE_AREA_START +
                (target_slot - 1) * cardtool.PERFORMANCE_SLOT_SIZE
            )

            old_target_slot_data_for_warning = self.working_data[
                target_offset_for_warning:
                target_offset_for_warning + cardtool.PERFORMANCE_SLOT_SIZE
            ]

            target_is_initial = cardtool.is_initial_performance_slot(
                self.working_data,
                target_slot,
            )

        except Exception as e:
            messagebox.showerror("Copy Performance failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Copy source Performance?",
                "This will copy the selected source Performance to an empty working slot.\n\n"
                f"Source slot: {source_slot:03d} ({source_state})\n"
                f"Working slot: {target_slot:03d} ({target_state})\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data_for_warning,
                cardtool.PERFORMANCE_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Performance?",
                [
                    (
                        "The selected working Performance slot is occupied.\n\n"
                        f"Source slot: {source_slot:03d} ({source_state})\n"
                        f"Working slot: {target_slot:03d} ({target_state})\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Performance will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The source card is read only and will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_performance_slot(
                source.data,
                self.working_data,
                source_slot,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy Performance failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Copy Performance aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy Performance failed",
                "The modified working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        new_target_data = output[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        source_slot_name = cardtool.decode_patch_name(
            source_slot_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )
        old_target_slot_name = cardtool.decode_patch_name(
            old_target_slot_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )
        new_target_slot_name = cardtool.decode_patch_name(
            new_target_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )

        if source_slot_name == "":
            source_slot_name = "<blank>"

        if old_target_slot_name == "":
            old_target_slot_name = "<blank>"

        if new_target_slot_name == "":
            new_target_slot_name = "<blank>"

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy Performance applied to working destination")
        self.log(f"  Source file:      {source.path}")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Source slot:      {source_slot:03d}  0x{source_offset:04X}  {source_slot_name}")
        self.log(f"  Working slot:     {target_slot:03d}  0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_slot_name}")
        self.log(f"  New working name: {new_target_slot_name}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def copy_source_patch_bank(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Copy Patch bank", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy Patch bank", "Load a base/destination card first.")
            return

        try:
            working_patch_bank_is_empty = all(
                cardtool.is_initial_patch_slot(self.working_data, slot)
                for slot in range(1, cardtool.PATCH_SLOT_COUNT + 1)
            )

        except Exception as e:
            messagebox.showerror("Copy Patch bank failed", str(e))
            return

        if working_patch_bank_is_empty:
            confirmed = messagebox.askyesno(
                "Copy entire Patch bank?",
                "This will copy all 64 source Patch slots into the empty working Patch bank.\n\n"
                f"Source card:\n{source.path}\n\n"
                "No existing working Patch slot will be overwritten.\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            confirmed = self.ask_colored_yesno(
                "Copy entire Patch bank?",
                [
                    (
                        "This will replace all 64 Patch slots in the working destination.\n\n"
                        f"Source card:\n{source.path}\n\n"
                        "If you continue, the entire working Patch bank will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The source card is read only and will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            output, source_offset, source_patch_data, old_target_patch_data = cardtool.copy_patch_bank_area(
                source.data,
                self.working_data,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy Patch bank failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Copy Patch bank aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy Patch bank failed",
                "The modified working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        if output == self.working_data:
            self.log("")
            self.log("Copy Patch bank skipped")
            self.log("  Reason:        source Patch bank already matches working destination")
            self.log(f"  Source file:   {source.path}")
            self.log("  File written:  no")
            return

        patch_area_size = (
            cardtool.PATCH_SLOT_COUNT *
            cardtool.PATCH_SLOT_SIZE
        )

        new_target_patch_data = output[
            cardtool.PATCH_AREA_START:
            cardtool.PATCH_AREA_START + patch_area_size
        ]

        old_digest = cardtool.fnv1a32(old_target_patch_data)
        new_digest = cardtool.fnv1a32(new_target_patch_data)

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy Patch bank applied to working destination")
        self.log(f"  Source file:      {source.path}")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Source offset:    0x{source_offset:04X}")
        self.log(f"  Area size:        {patch_area_size} bytes")
        self.log(f"  Slots copied:     {cardtool.PATCH_SLOT_COUNT}")
        self.log(f"  Old bank digest:  0x{old_digest:08X}")
        self.log(f"  New bank digest:  0x{new_digest:08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def swap_selected_patch(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Swap Patch", "Load a base/destination card first.")
            return

        try:
            slot_a = self._selected_tree_slot(
                self.base_patch_tree,
                "working Patch",
            )
        except Exception as e:
            messagebox.showerror("Swap Patch", str(e))
            return

        try:
            slot_b = self.choose_working_slot_dialog(
                "Swap Patch slots",
                "Choose the working Patch slot to swap with.\n\n"
                f"Selected slot: {slot_a:03d}\n\n"
                "All other working Patch slots are shown.\n"
                "No data will be lost: the two slots will be exchanged.",
                "patch",
                exclude_slot=slot_a,
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Swap Patch", str(e))
            return

        if slot_b is None:
            return

        try:
            output, offset_a, offset_b, old_slot_a_data, old_slot_b_data = cardtool.swap_patch_slots(
                self.working_data,
                slot_a,
                slot_b,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Swap Patch failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Swap Patch aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Swap Patch failed",
                "The swapped working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        old_name_a = cardtool.decode_patch_name(
            old_slot_a_data[:cardtool.PATCH_NAME_BYTES]
        )
        old_name_b = cardtool.decode_patch_name(
            old_slot_b_data[:cardtool.PATCH_NAME_BYTES]
        )

        if old_name_a == "":
            old_name_a = "<blank>"

        if old_name_b == "":
            old_name_b = "<blank>"

        new_slot_a_data = output[
            offset_a:
            offset_a + cardtool.PATCH_SLOT_SIZE
        ]
        new_slot_b_data = output[
            offset_b:
            offset_b + cardtool.PATCH_SLOT_SIZE
        ]

        new_name_a = cardtool.decode_patch_name(
            new_slot_a_data[:cardtool.PATCH_NAME_BYTES]
        )
        new_name_b = cardtool.decode_patch_name(
            new_slot_b_data[:cardtool.PATCH_NAME_BYTES]
        )

        if new_name_a == "":
            new_name_a = "<blank>"

        if new_name_b == "":
            new_name_b = "<blank>"

        if output == self.working_data:
            self.log("")
            self.log("Swap Patch skipped")
            self.log(f"  Slot A:        {slot_a:03d}  {old_name_a}")
            self.log(f"  Slot B:        {slot_b:03d}  {old_name_b}")
            self.log("  Reason:        slot data is identical")
            self.log("  File written:  no")
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Swap Patch applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot A:           {slot_a:03d}")
        self.log(f"  Offset A:         0x{offset_a:04X}")
        self.log(f"  Old name A:       {old_name_a}")
        self.log(f"  New name A:       {new_name_a}")
        self.log(f"  Old digest A:     0x{cardtool.fnv1a32(old_slot_a_data):08X}")
        self.log(f"  New digest A:     0x{cardtool.fnv1a32(new_slot_a_data):08X}")
        self.log("")
        self.log(f"  Slot B:           {slot_b:03d}")
        self.log(f"  Offset B:         0x{offset_b:04X}")
        self.log(f"  Old name B:       {old_name_b}")
        self.log(f"  New name B:       {new_name_b}")
        self.log(f"  Old digest B:     0x{cardtool.fnv1a32(old_slot_b_data):08X}")
        self.log(f"  New digest B:     0x{cardtool.fnv1a32(new_slot_b_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def move_selected_patch_to_empty(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Move Patch", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.base_patch_tree,
                "working Patch",
            )
        except Exception as e:
            messagebox.showerror("Move Patch", str(e))
            return

        try:
            if cardtool.is_initial_patch_slot(self.working_data, source_slot):
                messagebox.showerror(
                    "Move Patch",
                    "The selected working Patch slot is already INITIAL DATA.",
                )
                return

        except Exception as e:
            messagebox.showerror("Move Patch failed", str(e))
            return

        try:
            target_slot = self.choose_working_slot_dialog(
                "Move Patch to slot",
                "Choose the working Patch slot to move into.\n\n"
                f"Selected slot: {source_slot:03d}\n\n"
                "INITIAL DATA working slots will be used as empty slots.\n"
                "Occupied working slots will require an overwrite confirmation.",
                "patch",
                exclude_slot=source_slot,
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Move Patch", str(e))
            return

        if target_slot is None:
            return

        if source_slot == target_slot:
            messagebox.showerror(
                "Move Patch",
                "Selected and chosen Patch slots must be different.",
            )
            return

        source_offset = (
            cardtool.PATCH_AREA_START +
            (source_slot - 1) * cardtool.PATCH_SLOT_SIZE
        )
        target_offset = (
            cardtool.PATCH_AREA_START +
            (target_slot - 1) * cardtool.PATCH_SLOT_SIZE
        )

        source_slot_data = self.working_data[
            source_offset:
            source_offset + cardtool.PATCH_SLOT_SIZE
        ]
        old_target_slot_data = self.working_data[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]

        try:
            target_is_initial = cardtool.is_initial_patch_slot(
                self.working_data,
                target_slot,
            )

        except Exception as e:
            messagebox.showerror("Move Patch failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Move selected Patch?",
                "This will move the selected working Patch slot to the selected empty working slot.\n\n"
                f"Selected slot: {source_slot:03d}\n"
                f"Working slot: {target_slot:03d}\n\n"
                "The selected slot will become INITIAL DATA.\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

            if not confirmed:
                return

            try:
                (
                    output,
                    source_offset,
                    target_offset,
                    source_slot_data,
                    old_target_slot_data,
                    template_slot,
                    template_offset,
                    template_slot_data,
                ) = cardtool.move_patch_slot_to_empty(
                    self.working_data,
                    source_slot,
                    target_slot,
                )

                check_text, check_ok = cardtool.render_check_card_report(
                    self.base_card.path,
                    output,
                )

            except Exception as e:
                messagebox.showerror("Move Patch failed", str(e))
                return

            move_mode = "empty working slot"

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data,
                cardtool.PATCH_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Patch?",
                [
                    (
                        "The selected working Patch slot is occupied.\n\n"
                        f"Selected slot: {source_slot:03d}\n"
                        f"Working slot: {target_slot:03d}\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Patch will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The selected slot will become INITIAL DATA.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

            if not confirmed:
                return

            try:
                output_buffer = bytearray(self.working_data)
                output_buffer[
                    target_offset:
                    target_offset + cardtool.PATCH_SLOT_SIZE
                ] = source_slot_data

                (
                    output,
                    cleared_source_offset,
                    old_source_after_target_write_data,
                    template_slot,
                    template_offset,
                    template_slot_data,
                ) = cardtool.clear_patch_slot(
                    bytes(output_buffer),
                    source_slot,
                )

                if cleared_source_offset != source_offset:
                    raise ValueError("internal error: cleared source offset mismatch")

                check_text, check_ok = cardtool.render_check_card_report(
                    self.base_card.path,
                    output,
                )

            except Exception as e:
                messagebox.showerror("Move Patch failed", str(e))
                return

            move_mode = "overwrite working slot"

        if not check_ok:
            self.log("")
            self.log("Move Patch aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Move Patch failed",
                "The moved working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        new_source_slot_data = output[
            source_offset:
            source_offset + cardtool.PATCH_SLOT_SIZE
        ]
        new_target_slot_data = output[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]

        old_source_name = cardtool.slot_display_name(
            source_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )
        new_source_name = cardtool.slot_display_name(
            new_source_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )
        old_target_name = cardtool.slot_display_name(
            old_target_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )
        new_target_name = cardtool.slot_display_name(
            new_target_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )
        template_name = cardtool.slot_display_name(
            template_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )

        if output == self.working_data:
            self.log("")
            self.log("Move Patch skipped")
            self.log(f"  Selected slot: {source_slot:03d}")
            self.log(f"  Working slot:  {target_slot:03d}")
            self.log("  Reason:        output is unchanged")
            self.log("  File written:  no")
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Move Patch applied to working destination")
        self.log(f"  Mode:              {move_mode}")
        self.log(f"  Base file:         {self.base_card.path}")
        self.log(f"  Selected slot:     {source_slot:03d}")
        self.log(f"  Selected offset:   0x{source_offset:04X}")
        self.log(f"  Old selected name: {old_source_name}")
        self.log(f"  New selected name: {new_source_name}")
        self.log(f"  Old selected digest: 0x{cardtool.fnv1a32(source_slot_data):08X}")
        self.log(f"  New selected digest: 0x{cardtool.fnv1a32(new_source_slot_data):08X}")
        self.log("")
        self.log(f"  Working slot:      {target_slot:03d}")
        self.log(f"  Working offset:    0x{target_offset:04X}")
        self.log(f"  Old working name:  {old_target_name}")
        self.log(f"  New working name:  {new_target_name}")
        self.log(f"  Old working digest: 0x{cardtool.fnv1a32(old_target_slot_data):08X}")
        self.log(f"  New working digest: 0x{cardtool.fnv1a32(new_target_slot_data):08X}")
        self.log("")
        self.log(f"  Template slot:     {template_slot:03d}")
        self.log(f"  Template offset:   0x{template_offset:04X}")
        self.log(f"  Template name:     {template_name}")
        self.log(f"  Template digest:   0x{cardtool.fnv1a32(template_slot_data):08X}")
        self.log(f"  Working digest:    0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:        OK")
        self.log("  File written:      no")

    def clear_selected_patch(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Clear Patch", "Load a base/destination card first.")
            return

        try:
            target_slot = self._selected_tree_slot(
                self.base_patch_tree,
                "working Patch",
            )
        except Exception as e:
            messagebox.showerror("Clear Patch", str(e))
            return

        try:
            target_is_initial = cardtool.is_initial_patch_slot(
                self.working_data,
                target_slot,
            )

            target_offset_for_warning = (
                cardtool.PATCH_AREA_START +
                (target_slot - 1) * cardtool.PATCH_SLOT_SIZE
            )

            old_target_slot_data_for_warning = self.working_data[
                target_offset_for_warning:
                target_offset_for_warning + cardtool.PATCH_SLOT_SIZE
            ]

        except Exception as e:
            messagebox.showerror("Clear Patch failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Clear selected Patch?",
                "The selected working Patch slot already contains INITIAL DATA.\n\n"
                f"Slot: {target_slot:03d}\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data_for_warning,
                cardtool.PATCH_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Clear selected Patch?",
                [
                    (
                        "This will replace the selected working Patch slot "
                        "with the card's INITIAL DATA Patch template.\n\n"
                        f"Slot: {target_slot:03d}\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the selected Patch slot will be ",
                        None,
                    ),
                    ("cleared", "red"),
                    (
                        ".\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            (
                output,
                target_offset,
                old_target_slot_data,
                template_slot,
                template_offset,
                template_slot_data,
            ) = cardtool.clear_patch_slot(
                self.working_data,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Clear Patch failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Clear Patch aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Clear Patch failed",
                "The cleared working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        old_name = cardtool.slot_display_name(
            old_target_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )
        template_name = cardtool.slot_display_name(
            template_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )

        new_target_slot_data = output[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]
        new_name = cardtool.slot_display_name(
            new_target_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )

        if output == self.working_data:
            self.log("")
            self.log("Clear Patch skipped")
            self.log(f"  Slot:          {target_slot:03d}")
            self.log("  Reason:        slot already matches INITIAL DATA template")
            self.log(f"  Name:          {new_name}")
            self.log("  File written:  no")
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Clear Patch applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {target_slot:03d}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old name:         {old_name}")
        self.log(f"  New name:         {new_name}")
        self.log(f"  Old digest:       0x{cardtool.fnv1a32(old_target_slot_data):08X}")
        self.log(f"  New digest:       0x{cardtool.fnv1a32(new_target_slot_data):08X}")
        self.log("")
        self.log(f"  Template slot:    {template_slot:03d}")
        self.log(f"  Template offset:  0x{template_offset:04X}")
        self.log(f"  Template name:    {template_name}")
        self.log(f"  Template digest:  0x{cardtool.fnv1a32(template_slot_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def rename_selected_patch(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Rename Patch", "Load a base/destination card first.")
            return

        try:
            target_slot = self._selected_tree_slot(
                self.base_patch_tree,
                "working Patch",
            )
        except Exception as e:
            messagebox.showerror("Rename Patch", str(e))
            return

        target_offset = (
            cardtool.PATCH_AREA_START +
            (target_slot - 1) * cardtool.PATCH_SLOT_SIZE
        )

        old_slot_data = self.working_data[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]

        old_name = cardtool.decode_patch_name(
            old_slot_data[:cardtool.PATCH_NAME_BYTES]
        )

        if old_name == "":
            old_name = "<blank>"

        initial_name = "" if old_name == "<blank>" else old_name.rstrip()

        new_name = simpledialog.askstring(
            "Rename Patch",
            "New Patch name\n"
            f"Slot: {target_slot:03d}\n\n"
            "Maximum 12 printable ASCII characters.",
            initialvalue=initial_name,
            parent=self,
        )

        if new_name is None:
            return

        new_name = new_name.strip()

        try:
            output, name_offset, old_name_data, new_name_data = cardtool.set_patch_name(
                self.working_data,
                target_slot,
                new_name,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Rename Patch failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Rename Patch aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Rename Patch failed",
                "The renamed working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        if output == self.working_data:
            self.log("")
            self.log("Rename Patch skipped")
            self.log(f"  Slot:          {target_slot:03d}")
            self.log("  Reason:        name is unchanged")
            self.log(f"  Name:          {old_name}")
            self.log("  File written:  no")
            return

        new_slot_data = output[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]

        decoded_old_name = cardtool.decode_patch_name(old_name_data)
        decoded_new_name = cardtool.decode_patch_name(new_name_data)

        if decoded_old_name == "":
            decoded_old_name = "<blank>"

        if decoded_new_name == "":
            decoded_new_name = "<blank>"

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Rename Patch applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {target_slot:03d}")
        self.log(f"  Name offset:      0x{name_offset:04X}")
        self.log(f"  Old name:         {decoded_old_name}")
        self.log(f"  New name:         {decoded_new_name}")
        self.log(f"  Old slot digest:  0x{cardtool.fnv1a32(old_slot_data):08X}")
        self.log(f"  New slot digest:  0x{cardtool.fnv1a32(new_slot_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def copy_working_patch_to_slot(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy working Patch", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.base_patch_tree,
                "working Patch",
            )
        except Exception as e:
            messagebox.showerror("Copy working Patch", str(e))
            return

        try:
            target_slot = self.choose_working_slot_dialog(
                "Copy working Patch to slot",
                "Choose the working Patch slot to copy into.\n\n"
                f"Selected slot: {source_slot:03d}\n\n"
                "All other working Patch slots are shown.\n"
                "The chosen working slot will be replaced in RAM after confirmation.",
                "patch",
                exclude_slot=source_slot,
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Copy working Patch", str(e))
            return

        if target_slot is None:
            return

        if source_slot == target_slot:
            messagebox.showerror(
                "Copy working Patch",
                "Selected and chosen Patch slots must be different.",
            )
            return

        try:
            source_is_initial = cardtool.is_initial_patch_slot(
                self.working_data,
                source_slot,
            )
            target_is_initial = cardtool.is_initial_patch_slot(
                self.working_data,
                target_slot,
            )

            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_patch_slot(
                self.working_data,
                self.working_data,
                source_slot,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy working Patch failed", str(e))
            return

        source_state = "INITIAL" if source_is_initial else "occupied"
        target_state = "INITIAL" if target_is_initial else "occupied"

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Copy working Patch?",
                "This will copy one working Patch slot to an empty working slot.\n\n"
                f"Selected slot: {source_slot:03d} ({source_state})\n"
                f"Working slot: {target_slot:03d} ({target_state})\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data,
                cardtool.PATCH_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Patch?",
                [
                    (
                        "The chosen working Patch slot is occupied.\n\n"
                        f"Selected slot: {source_slot:03d} ({source_state})\n"
                        f"Working slot: {target_slot:03d} ({target_state})\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Patch will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The selected slot will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        if not check_ok:
            self.log("")
            self.log("Copy working Patch aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy working Patch failed",
                "The copied working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        source_name = cardtool.slot_display_name(
            source_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )
        old_target_name = cardtool.slot_display_name(
            old_target_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )

        if output == self.working_data:
            self.log("")
            self.log("Copy working Patch skipped")
            self.log(f"  Selected slot: {source_slot:03d}  {source_name}")
            self.log(f"  Working slot:  {target_slot:03d}  {old_target_name}")
            self.log("  Reason:        selected slot already matches working slot")
            self.log("  File written:  no")
            return

        new_target_slot_data = output[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]
        new_target_name = cardtool.slot_display_name(
            new_target_slot_data,
            cardtool.PATCH_NAME_BYTES,
        )

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy working Patch applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Selected slot:    {source_slot:03d}")
        self.log(f"  Selected offset:  0x{source_offset:04X}")
        self.log(f"  Selected name:    {source_name}")
        self.log(f"  Selected state:   {source_state}")
        self.log(f"  Selected digest:  0x{cardtool.fnv1a32(source_slot_data):08X}")
        self.log("")
        self.log(f"  Working slot:     {target_slot:03d}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_name}")
        self.log(f"  New working name: {new_target_name}")
        self.log(f"  Old working state: {target_state}")
        self.log(f"  Old working digest: 0x{cardtool.fnv1a32(old_target_slot_data):08X}")
        self.log(f"  New working digest: 0x{cardtool.fnv1a32(new_target_slot_data):08X}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def copy_selected_patch_to_same_slot(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Copy Patch", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy Patch", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.source_patch_tree,
                "source Patch",
            )

        except Exception as e:
            messagebox.showerror("Copy Patch", str(e))
            return

        try:
            source_state = self.slot_state_label(source.data, "patch", source_slot)
            target_state = self.slot_state_label(self.working_data, "patch", source_slot)

            if source_state == "":
                source_state = "occupied"

            if target_state == "":
                target_state = "occupied"

            target_offset_for_warning = (
                cardtool.PATCH_AREA_START +
                (source_slot - 1) * cardtool.PATCH_SLOT_SIZE
            )

            old_target_slot_data_for_warning = self.working_data[
                target_offset_for_warning:
                target_offset_for_warning + cardtool.PATCH_SLOT_SIZE
            ]

            target_is_initial = cardtool.is_initial_patch_slot(
                self.working_data,
                source_slot,
            )

        except Exception as e:
            messagebox.showerror("Copy Patch failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Copy source Patch to same slot?",
                "This will copy the selected source Patch to the same empty working slot.\n\n"
                f"Slot: {source_slot:03d}\n"
                f"Source state: {source_state}\n"
                f"Working state: {target_state}\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data_for_warning,
                cardtool.PATCH_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Patch?",
                [
                    (
                        "The corresponding working Patch slot is occupied.\n\n"
                        f"Slot: {source_slot:03d}\n"
                        f"Source state: {source_state}\n"
                        f"Working state: {target_state}\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Patch will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The source card is read only and will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_patch_slot(
                source.data,
                self.working_data,
                source_slot,
                source_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy Patch failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Copy Patch same slot aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy Patch failed",
                "The modified working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        source_slot_name = cardtool.decode_patch_name(
            source_slot_data[:cardtool.PATCH_NAME_BYTES]
        )
        old_target_slot_name = cardtool.decode_patch_name(
            old_target_slot_data[:cardtool.PATCH_NAME_BYTES]
        )

        if source_slot_name == "":
            source_slot_name = "<blank>"

        if old_target_slot_name == "":
            old_target_slot_name = "<blank>"

        if output == self.working_data:
            self.log("")
            self.log("Copy Patch same slot skipped")
            self.log(f"  Slot:          {source_slot:03d}")
            self.log("  Reason:        source slot already matches working destination")
            self.log(f"  Name:          {source_slot_name}")
            self.log("  File written:  no")
            return

        new_target_data = output[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]

        new_target_slot_name = cardtool.decode_patch_name(
            new_target_data[:cardtool.PATCH_NAME_BYTES]
        )

        if new_target_slot_name == "":
            new_target_slot_name = "<blank>"

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy Patch same slot applied to working destination")
        self.log(f"  Source file:      {source.path}")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {source_slot:03d}")
        self.log(f"  Source offset:    0x{source_offset:04X}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_slot_name}")
        self.log(f"  New working name: {new_target_slot_name}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def copy_selected_patch(self):
        source = self.selected_source_card()

        if source is None:
            messagebox.showerror("Copy Patch", "Load and select a source card first.")
            return

        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Copy Patch", "Load a base/destination card first.")
            return

        try:
            source_slot = self._selected_tree_slot(
                self.source_patch_tree,
                "source Patch",
            )

        except Exception as e:
            messagebox.showerror("Copy Patch", str(e))
            return

        try:
            target_slot = self.choose_working_slot_dialog(
                "Copy source Patch to slot",
                "Choose the working Patch slot to replace.\n\n"
                f"Selected source slot: {source_slot:03d}\n\n"
                "The selected working slot will be replaced in RAM after confirmation.",
                "patch",
                require_initial=False,
            )

        except Exception as e:
            messagebox.showerror("Copy Patch", str(e))
            return

        if target_slot is None:
            return

        try:
            source_state = self.slot_state_label(source.data, "patch", source_slot)
            target_state = self.slot_state_label(self.working_data, "patch", target_slot)

            if source_state == "":
                source_state = "occupied"

            if target_state == "":
                target_state = "occupied"

            target_offset_for_warning = (
                cardtool.PATCH_AREA_START +
                (target_slot - 1) * cardtool.PATCH_SLOT_SIZE
            )

            old_target_slot_data_for_warning = self.working_data[
                target_offset_for_warning:
                target_offset_for_warning + cardtool.PATCH_SLOT_SIZE
            ]

            target_is_initial = cardtool.is_initial_patch_slot(
                self.working_data,
                target_slot,
            )

        except Exception as e:
            messagebox.showerror("Copy Patch failed", str(e))
            return

        if target_is_initial:
            confirmed = messagebox.askyesno(
                "Copy source Patch?",
                "This will copy the selected source Patch to an empty working slot.\n\n"
                f"Source slot: {source_slot:03d} ({source_state})\n"
                f"Working slot: {target_slot:03d} ({target_state})\n\n"
                "No file will be written now.\n\n"
                "Continue?",
            )

        else:
            old_target_name_for_warning = cardtool.slot_display_name(
                old_target_slot_data_for_warning,
                cardtool.PATCH_NAME_BYTES,
            )

            confirmed = self.ask_colored_yesno(
                "Overwrite working Patch?",
                [
                    (
                        "The selected working Patch slot is occupied.\n\n"
                        f"Source slot: {source_slot:03d} ({source_state})\n"
                        f"Working slot: {target_slot:03d} ({target_state})\n"
                        f"Working name: {old_target_name_for_warning}\n\n"
                        "If you continue, the working Patch will be ",
                        None,
                    ),
                    ("overwritten", "red"),
                    (
                        ".\n"
                        "The source card is read only and will remain unchanged.\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

        if not confirmed:
            return

        try:
            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_patch_slot(
                source.data,
                self.working_data,
                source_slot,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Copy Patch failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Copy Patch aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Copy Patch failed",
                "The modified working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        new_target_data = output[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]

        source_slot_name = cardtool.decode_patch_name(
            source_slot_data[:cardtool.PATCH_NAME_BYTES]
        )
        old_target_slot_name = cardtool.decode_patch_name(
            old_target_slot_data[:cardtool.PATCH_NAME_BYTES]
        )
        new_target_slot_name = cardtool.decode_patch_name(
            new_target_data[:cardtool.PATCH_NAME_BYTES]
        )

        if source_slot_name == "":
            source_slot_name = "<blank>"

        if old_target_slot_name == "":
            old_target_slot_name = "<blank>"

        if new_target_slot_name == "":
            new_target_slot_name = "<blank>"

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Copy Patch applied to working destination")
        self.log(f"  Source file:      {source.path}")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Source slot:      {source_slot:03d}  0x{source_offset:04X}  {source_slot_name}")
        self.log(f"  Working slot:     {target_slot:03d}  0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_slot_name}")
        self.log(f"  New working name: {new_target_slot_name}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def restore_selected_patch_from_base(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Restore Patch", "Load a base/destination card first.")
            return

        try:
            target_slot = self._selected_tree_slot(
                self.base_patch_tree,
                "working Patch",
            )

            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_patch_slot(
                self.base_card.data,
                self.working_data,
                target_slot,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Restore Patch failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Restore Patch aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Restore Patch failed",
                "The restored working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        restored_data = output[
            target_offset:
            target_offset + cardtool.PATCH_SLOT_SIZE
        ]

        base_slot_name = cardtool.decode_patch_name(
            source_slot_data[:cardtool.PATCH_NAME_BYTES]
        )
        old_target_slot_name = cardtool.decode_patch_name(
            old_target_slot_data[:cardtool.PATCH_NAME_BYTES]
        )
        restored_slot_name = cardtool.decode_patch_name(
            restored_data[:cardtool.PATCH_NAME_BYTES]
        )

        if base_slot_name == "":
            base_slot_name = "<blank>"

        if old_target_slot_name == "":
            old_target_slot_name = "<blank>"

        if restored_slot_name == "":
            restored_slot_name = "<blank>"

        if output == self.working_data:
            self.log("")
            self.log("Restore Patch skipped")
            self.log(f"  Slot:          {target_slot:03d}")
            self.log("  Reason:        working slot already matches base")
            self.log(f"  Name:          {restored_slot_name}")
            self.log("  File written:  no")
            return

        confirmed = self.ask_colored_yesno(
            "Restore selected Patch?",
            [
                (
                    "This will restore the selected working Patch slot "
                    "from the loaded base card.\n\n"
                    f"Slot: {target_slot:03d}\n"
                    f"Working name: {old_target_slot_name}\n"
                    f"Base name: {base_slot_name}\n\n"
                    "If you continue, the working RAM change for this slot will be ",
                    None,
                ),
                ("discarded", "red"),
                (
                    ".\n"
                    "No file will be written now.\n\n"
                    "Continue?",
                    None,
                ),
            ],
        )

        if not confirmed:
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Restore Patch applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {target_slot:03d}")
        self.log(f"  Base offset:      0x{source_offset:04X}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_slot_name}")
        self.log(f"  Restored name:    {restored_slot_name}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def restore_selected_performance_from_base(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Restore Performance", "Load a base/destination card first.")
            return

        try:
            target_slot = self._selected_tree_slot(
                self.base_perf_tree,
                "working Performance",
            )

            output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_performance_slot(
                self.base_card.data,
                self.working_data,
                target_slot,
                target_slot,
            )

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Restore Performance failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Restore Performance aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Restore Performance failed",
                "The restored working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        restored_data = output[
            target_offset:
            target_offset + cardtool.PERFORMANCE_SLOT_SIZE
        ]

        base_slot_name = cardtool.decode_patch_name(
            source_slot_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )
        old_target_slot_name = cardtool.decode_patch_name(
            old_target_slot_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )
        restored_slot_name = cardtool.decode_patch_name(
            restored_data[:cardtool.PERFORMANCE_NAME_BYTES]
        )

        if base_slot_name == "":
            base_slot_name = "<blank>"

        if old_target_slot_name == "":
            old_target_slot_name = "<blank>"

        if restored_slot_name == "":
            restored_slot_name = "<blank>"

        if output == self.working_data:
            self.log("")
            self.log("Restore Performance skipped")
            self.log(f"  Slot:          {target_slot:03d}")
            self.log("  Reason:        working slot already matches base")
            self.log(f"  Name:          {restored_slot_name}")
            self.log("  File written:  no")
            return

        confirmed = self.ask_colored_yesno(
            "Restore selected Performance?",
            [
                (
                    "This will restore the selected working Performance slot "
                    "from the loaded base card.\n\n"
                    f"Slot: {target_slot:03d}\n"
                    f"Working name: {old_target_slot_name}\n"
                    f"Base name: {base_slot_name}\n\n"
                    "If you continue, the working RAM change for this slot will be ",
                    None,
                ),
                ("discarded", "red"),
                (
                    ".\n"
                    "No file will be written now.\n\n"
                    "Continue?",
                    None,
                ),
            ],
        )

        if not confirmed:
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Restore Performance applied to working destination")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {target_slot:03d}")
        self.log(f"  Base offset:      0x{source_offset:04X}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_slot_name}")
        self.log(f"  Restored name:    {restored_slot_name}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def collect_changed_slots(self):
        if self.base_card is None or self.working_data is None:
            return []

        changes = []

        area_specs = [
            (
                "Performance",
                cardtool.PERFORMANCE_AREA_START,
                cardtool.PERFORMANCE_SLOT_SIZE,
                cardtool.PERFORMANCE_NAME_BYTES,
                cardtool.PERFORMANCE_SLOT_COUNT,
            ),
            (
                "Patch",
                cardtool.PATCH_AREA_START,
                cardtool.PATCH_SLOT_SIZE,
                cardtool.PATCH_NAME_BYTES,
                cardtool.PATCH_SLOT_COUNT,
            ),
        ]

        for area_name, area_start, slot_size, name_bytes, slot_count in area_specs:
            for slot in range(slot_count):
                offset = area_start + slot * slot_size

                base_slot_data = self.base_card.data[offset:offset + slot_size]
                working_slot_data = self.working_data[offset:offset + slot_size]

                if len(base_slot_data) != slot_size or len(working_slot_data) != slot_size:
                    continue

                if base_slot_data == working_slot_data:
                    continue

                base_name = cardtool.decode_patch_name(base_slot_data[:name_bytes])
                working_name = cardtool.decode_patch_name(working_slot_data[:name_bytes])

                if base_name == "":
                    base_name = "<blank>"

                if working_name == "":
                    working_name = "<blank>"

                changes.append(
                    (
                        area_name,
                        slot + 1,
                        base_name,
                        working_name,
                        cardtool.fnv1a32(base_slot_data),
                        cardtool.fnv1a32(working_slot_data),
                    )
                )

        return changes

    def refresh_changed_slots(self):
        for item in self.changed_tree.get_children():
            self.changed_tree.delete(item)

        changes = self.collect_changed_items()

        for area_name, slot, base_name, working_name, base_digest, working_digest in changes:
            item_id = f"{area_name}:{slot}"

            self.changed_tree.insert(
                "",
                tk.END,
                iid=item_id,
                values=(
                    area_name,
                    f"{slot:03d}",
                    base_name,
                    working_name,
                    f"0x{base_digest:08X}",
                    f"0x{working_digest:08X}",
                ),
            )

    def select_changed_slot_in_destination(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Select changed item", "Load a base/destination card first.")
            return

        selected = self.changed_tree.selection()

        if not selected:
            messagebox.showerror("Select changed item", "Select one changed item first.")
            return

        values = self.changed_tree.item(selected[0], "values")

        if len(values) < 2:
            messagebox.showerror("Select changed item", "Invalid changed item selection.")
            return

        area_name = values[0]

        try:
            target_slot = int(values[1])
        except Exception:
            messagebox.showerror("Select changed item", "Invalid changed item number.")
            return

        if area_name == "Rhythm":
            self.view_working_rhythm_info_report()

            self.log("")
            self.log("Selected changed Rhythm item")
            self.log(f"  Area:          {area_name}")
            self.log(f"  Offset:        0x{cardtool.RHYTHM_AREA_START:04X}")
            self.log(f"  Size:          {cardtool.RHYTHM_AREA_SIZE} bytes")
            self.log("  File written:  no")
            return

        item_id = str(target_slot)

        if area_name == "Performance":
            target_tree = self.base_perf_tree
            self.notebook.select(self.performance_frame)
        elif area_name == "Patch":
            target_tree = self.base_patch_tree
            self.notebook.select(self.patch_frame)
        else:
            messagebox.showerror("Select changed item", f"Unknown changed item area: {area_name}")
            return

        if not target_tree.exists(item_id):
            messagebox.showerror(
                "Select changed item",
                f"Working slot not found: {area_name} {target_slot:03d}",
            )
            return

        target_tree.selection_set(item_id)
        target_tree.focus(item_id)
        target_tree.see(item_id)

        self.log("")
        self.log("Selected changed item in working destination")
        self.log(f"  Area:          {area_name}")
        self.log(f"  Slot:          {target_slot:03d}")
        self.log("  File written:  no")

    def save_change_report(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Save change report", "Load a base/destination card first.")
            return

        changes = self.collect_changed_items()

        filename = filedialog.asksaveasfilename(
            title="Save CardRAM change report",
            initialdir=str(self.base_card.path.parent),
            initialfile=f"{self.base_card.path.stem}_changes.txt",
            defaultextension=".txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
            confirmoverwrite=True,
        )

        if not filename:
            return

        output_path = Path(filename)

        try:
            lines = []
            lines.append("MiniJV880 CardRAM change report")
            lines.append("")
            lines.append(f"Base file:       {self.base_card.path}")
            lines.append(f"Base digest:     0x{cardtool.fnv1a32(self.base_card.data):08X}")
            lines.append(f"Working digest:  0x{cardtool.fnv1a32(self.working_data):08X}")
            lines.append(f"Changed items:   {len(changes)}")
            lines.append("")

            if not changes:
                lines.append("No changed items.")
            else:
                lines.append("Area         #     Base item             Working item          Base digest  Working digest")
                lines.append("-----------  ----  --------------------  --------------------  -----------  --------------")

                for area_name, slot, base_name, working_name, base_digest, working_digest in changes:
                    lines.append(
                        f"{area_name:<11}  "
                        f"{slot:03d}   "
                        f"{base_name[:20]:<20}  "
                        f"{working_name[:20]:<20}  "
                        f"0x{base_digest:08X}   "
                        f"0x{working_digest:08X}"
                    )

            lines.append("")

            output_path.write_text("\n".join(lines), encoding="utf-8")

        except Exception as e:
            messagebox.showerror("Save change report failed", str(e))
            return

        self.log("")
        self.log("Change report saved")
        self.log(f"  Report file:    {output_path}")
        self.log(f"  Changed items:  {len(changes)}")

        messagebox.showinfo(
            "Save change report",
            "Change report saved.\n\n"
            f"{output_path}",
        )

    def restore_selected_changed_slot_from_base(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Restore changed item", "Load a base/destination card first.")
            return

        selected = self.changed_tree.selection()

        if not selected:
            messagebox.showerror("Restore changed item", "Select one changed item first.")
            return

        values = self.changed_tree.item(selected[0], "values")

        if len(values) < 2:
            messagebox.showerror("Restore changed item", "Invalid changed item selection.")
            return

        area_name = values[0]

        if area_name == "Rhythm":
            start = cardtool.RHYTHM_AREA_START
            end = start + cardtool.RHYTHM_AREA_SIZE

            base_rhythm_data = self.base_card.data[start:end]
            working_rhythm_data = self.working_data[start:end]

            if len(base_rhythm_data) != cardtool.RHYTHM_AREA_SIZE:
                messagebox.showerror(
                    "Restore Rhythm failed",
                    "Base Rhythm area is incomplete.",
                )
                return

            if len(working_rhythm_data) != cardtool.RHYTHM_AREA_SIZE:
                messagebox.showerror(
                    "Restore Rhythm failed",
                    "Working Rhythm area is incomplete.",
                )
                return

            if base_rhythm_data == working_rhythm_data:
                self.log("")
                self.log("Restore Rhythm skipped")
                self.log("  Reason:        working Rhythm already matches base")
                self.log(f"  Rhythm digest: 0x{cardtool.fnv1a32(working_rhythm_data):08X}")
                self.log("  File written:  no")
                return

            confirmed = self.ask_colored_yesno(
                "Restore Rhythm from base?",
                [
                    (
                        "This will restore the raw Rhythm area in the working destination "
                        "from the loaded base card.\n\n"
                        f"Rhythm area: 0x{start:04X}..0x{end - 1:04X}"
                        f" ({cardtool.RHYTHM_AREA_SIZE} bytes)\n\n"
                        f"Working Rhythm digest: 0x{cardtool.fnv1a32(working_rhythm_data):08X}\n"
                        f"Base Rhythm digest:    0x{cardtool.fnv1a32(base_rhythm_data):08X}\n\n"
                        "If you continue, the working RAM Rhythm change will be ",
                        None,
                    ),
                    ("discarded", "red"),
                    (
                        ".\n"
                        "No file will be written now.\n\n"
                        "Continue?",
                        None,
                    ),
                ],
            )

            if not confirmed:
                return

            output = bytearray(self.working_data)
            output[start:end] = base_rhythm_data
            output = bytes(output)

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

            if not check_ok:
                self.log("")
                self.log("Restore Rhythm aborted: check-card returned WARN.")
                self.log(check_text)
                messagebox.showerror(
                    "Restore Rhythm failed",
                    "The restored working destination did not pass check-card.\n\n"
                    "The working destination was not changed.",
                )
                return

            old_working_digest = cardtool.fnv1a32(self.working_data)

            self.working_data = output
            self.working_dirty = True
            self.working_change_count += 1

            self.update_base_label()
            self.refresh_lists()

            self.log("")
            self.log("Restore Rhythm applied to working destination")
            self.log(f"  Base file:          {self.base_card.path}")
            self.log(f"  Rhythm area:        0x{start:04X}..0x{end - 1:04X}")
            self.log(f"  Area size:          {cardtool.RHYTHM_AREA_SIZE} bytes")
            self.log(f"  Old Rhythm digest:  0x{cardtool.fnv1a32(working_rhythm_data):08X}")
            self.log(f"  New Rhythm digest:  0x{cardtool.fnv1a32(base_rhythm_data):08X}")
            self.log(f"  Old working digest: 0x{old_working_digest:08X}")
            self.log(f"  New working digest: 0x{cardtool.fnv1a32(self.working_data):08X}")
            self.log("  check-card:         OK")
            self.log("  File written:       no")
            self.log("  Save required:      yes")
            return

        try:
            target_slot = int(values[1])
        except Exception:
            messagebox.showerror("Restore changed item", "Invalid changed item number.")
            return

        try:
            if area_name == "Performance":
                output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_performance_slot(
                    self.base_card.data,
                    self.working_data,
                    target_slot,
                    target_slot,
                )
                slot_size = cardtool.PERFORMANCE_SLOT_SIZE
                name_bytes = cardtool.PERFORMANCE_NAME_BYTES

            elif area_name == "Patch":
                output, source_offset, target_offset, source_slot_data, old_target_slot_data = cardtool.copy_patch_slot(
                    self.base_card.data,
                    self.working_data,
                    target_slot,
                    target_slot,
                )
                slot_size = cardtool.PATCH_SLOT_SIZE
                name_bytes = cardtool.PATCH_NAME_BYTES

            else:
                raise ValueError(f"unknown changed item area: {area_name}")

            check_text, check_ok = cardtool.render_check_card_report(
                self.base_card.path,
                output,
            )

        except Exception as e:
            messagebox.showerror("Restore changed item failed", str(e))
            return

        if not check_ok:
            self.log("")
            self.log("Restore changed item aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Restore changed item failed",
                "The restored working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
            )
            return

        restored_data = output[target_offset:target_offset + slot_size]

        base_slot_name = cardtool.decode_patch_name(
            source_slot_data[:name_bytes]
        )
        old_target_slot_name = cardtool.decode_patch_name(
            old_target_slot_data[:name_bytes]
        )
        restored_slot_name = cardtool.decode_patch_name(
            restored_data[:name_bytes]
        )

        if base_slot_name == "":
            base_slot_name = "<blank>"

        if old_target_slot_name == "":
            old_target_slot_name = "<blank>"

        if restored_slot_name == "":
            restored_slot_name = "<blank>"

        if output == self.working_data:
            self.log("")
            self.log("Restore changed item skipped")
            self.log(f"  Area:          {area_name}")
            self.log(f"  Slot:          {target_slot:03d}")
            self.log("  Reason:        slot already matches base")
            self.log(f"  Name:          {restored_slot_name}")
            self.log("  File written:  no")
            return

        confirmed = self.ask_colored_yesno(
            "Restore selected changed item?",
            [
                (
                    "This will restore the selected changed item "
                    "from the loaded base card.\n\n"
                    f"Area: {area_name}\n"
                    f"Slot: {target_slot:03d}\n"
                    f"Working name: {old_target_slot_name}\n"
                    f"Base name: {base_slot_name}\n\n"
                    "If you continue, the working RAM change for this slot will be ",
                    None,
                ),
                ("discarded", "red"),
                (
                    ".\n"
                    "No file will be written now.\n\n"
                    "Continue?",
                    None,
                ),
            ],
        )

        if not confirmed:
            return

        self.working_data = output
        self.working_dirty = True
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Restore changed item applied to working destination")
        self.log(f"  Area:             {area_name}")
        self.log(f"  Base file:        {self.base_card.path}")
        self.log(f"  Slot:             {target_slot:03d}")
        self.log(f"  Base offset:      0x{source_offset:04X}")
        self.log(f"  Working offset:   0x{target_offset:04X}")
        self.log(f"  Old working name: {old_target_slot_name}")
        self.log(f"  Restored name:    {restored_slot_name}")
        self.log(f"  Working digest:   0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:       OK")
        self.log("  File written:     no")

    def restore_all_changed_slots_from_base(self):
        if self.base_card is None or self.working_data is None:
            messagebox.showerror("Restore changed items", "Load a base/destination card first.")
            return

        slot_changes = self.collect_changed_slots()
        item_changes = self.collect_changed_items()

        if not item_changes:
            self.log("")
            self.log("Restore changed items skipped")
            self.log("  Reason:        working destination already matches base")
            self.log("  File written:  no")
            return

        performance_count = sum(1 for change in slot_changes if change[0] == "Performance")
        patch_count = sum(1 for change in slot_changes if change[0] == "Patch")
        rhythm_count = sum(1 for change in item_changes if change[0] == "Rhythm")

        if self.restore_changed_slots_window is not None:
            try:
                if self.restore_changed_slots_window.winfo_exists():
                    if self.restore_changed_slots_window.state() == "withdrawn":
                        self.restore_changed_slots_window.deiconify()

                    self.restore_changed_slots_window.lift(self)
                    self.restore_changed_slots_window.focus_set()
                    return

            except tk.TclError:
                pass

            self.restore_changed_slots_window = None

        window = tk.Toplevel(self)
        window.title("Restore changed items from base")
        self.restore_changed_slots_window = window

        window.geometry(self.restore_changed_slots_window_geometry or "560x260")
        window.minsize(520, 240)
        window.transient(self)
        window.resizable(True, True)

        def remember_restore_changed_slots_window_geometry():
            if not window.winfo_exists():
                return

            try:
                if window.state() == "normal":
                    self.restore_changed_slots_window_geometry = window.geometry()

            except tk.TclError:
                pass

        def close_restore_changed_slots_window():
            remember_restore_changed_slots_window_geometry()
            self.restore_changed_slots_window = None
            window.destroy()

        window.protocol("WM_DELETE_WINDOW", close_restore_changed_slots_window)

        window.bind(
            "<Configure>",
            lambda _event: remember_restore_changed_slots_window_geometry(),
        )

        frame = ttk.Frame(window, padding=12)
        frame.pack(fill=tk.BOTH, expand=True)

        ttk.Label(
            frame,
            text="Restore changed items from base",
            font=("TkDefaultFont", 11, "bold"),
        ).pack(anchor=tk.W)

        ttk.Label(
            frame,
            text=(
                "Choose which working RAM changes should be discarded by restoring them from base.\n"
                "No file will be written now."
            ),
        ).pack(anchor=tk.W, pady=(4, 12))

        summary = ttk.LabelFrame(frame, text="Changed items", padding=8)
        summary.pack(fill=tk.X)

        ttk.Label(
            summary,
            text=f"Performance: {performance_count}",
        ).pack(anchor=tk.W)

        ttk.Label(
            summary,
            text=f"Patch: {patch_count}",
        ).pack(anchor=tk.W)

        ttk.Label(
            summary,
            text=f"Rhythm: {rhythm_count}",
        ).pack(anchor=tk.W)

        actions = ttk.Frame(frame)
        actions.pack(fill=tk.X, pady=(12, 0))

        ttk.Button(
            actions,
            text="Performance",
            command=lambda: self.restore_changed_slots_from_base_scope(
                window,
                "Performance",
            ),
        ).pack(side=tk.LEFT)

        ttk.Button(
            actions,
            text="Patch",
            command=lambda: self.restore_changed_slots_from_base_scope(
                window,
                "Patch",
            ),
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            actions,
            text="Perf+Patch",
            command=lambda: self.restore_changed_slots_from_base_scope(
                window,
                "Both",
            ),
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            actions,
            text="Rhythm",
            command=lambda: self.restore_changed_slots_from_base_scope(
                window,
                "Rhythm",
            ),
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            actions,
            text="All",
            command=lambda: self.restore_changed_slots_from_base_scope(
                window,
                "All",
            ),
        ).pack(side=tk.LEFT, padx=(8, 0))

        ttk.Button(
            actions,
            text="Cancel",
            command=close_restore_changed_slots_window,
        ).pack(side=tk.RIGHT)

    def restore_changed_slots_from_base_scope(self, window, scope):
        slot_changes = self.collect_changed_slots()
        item_changes = self.collect_changed_items()

        if scope == "Performance":
            selected_changes = [
                change for change in slot_changes
                if change[0] == "Performance"
            ]
            title = "Restore Performance changes?"
            scope_text = "Performance"
            count_text = "Changed slots to restore"

        elif scope == "Patch":
            selected_changes = [
                change for change in slot_changes
                if change[0] == "Patch"
            ]
            title = "Restore Patch changes?"
            scope_text = "Patch"
            count_text = "Changed slots to restore"

        elif scope == "Both":
            selected_changes = slot_changes
            title = "Restore Performance and Patch changes?"
            scope_text = "Performance and Patch"
            count_text = "Changed slots to restore"

        elif scope == "Rhythm":
            selected_changes = [
                change for change in item_changes
                if change[0] == "Rhythm"
            ]
            title = "Restore Rhythm change?"
            scope_text = "Rhythm"
            count_text = "Changed items to restore"

        elif scope == "All":
            selected_changes = item_changes
            title = "Restore all changed items?"
            scope_text = "Performance, Patch and Rhythm"
            count_text = "Changed items to restore"

        else:
            messagebox.showerror("Restore changed items", f"Unknown restore scope: {scope}")
            return

        if not selected_changes:
            self.log("")
            self.log("Restore changed items skipped")
            self.log(f"  Scope:         {scope_text}")
            self.log("  Reason:        no changed items in selected scope")
            self.log("  File written:  no")
            self.restore_changed_slots_window = None
            window.destroy()
            return

        confirmed = self.ask_colored_yesno(
            title,
            [
                (
                    f"This will restore selected working {scope_text} changes "
                    "from the base card.\n\n"
                    f"{count_text}: {len(selected_changes)}\n\n"
                    "If you continue, the selected working RAM changes will be ",
                    None,
                ),
                ("discarded", "red"),
                (
                    ".\n"
                    "No file will be written now.\n\n"
                    "Continue?",
                    None,
                ),
            ],
            parent=window,
        )

        if not confirmed:
            return

        output = bytearray(self.working_data)

        for area_name, slot, _base_name, _working_name, _base_digest, _working_digest in selected_changes:
            if area_name == "Performance":
                offset = (
                    cardtool.PERFORMANCE_AREA_START +
                    (slot - 1) * cardtool.PERFORMANCE_SLOT_SIZE
                )
                item_size = cardtool.PERFORMANCE_SLOT_SIZE

            elif area_name == "Patch":
                offset = (
                    cardtool.PATCH_AREA_START +
                    (slot - 1) * cardtool.PATCH_SLOT_SIZE
                )
                item_size = cardtool.PATCH_SLOT_SIZE

            elif area_name == "Rhythm":
                offset = cardtool.RHYTHM_AREA_START
                item_size = cardtool.RHYTHM_AREA_SIZE

            else:
                messagebox.showerror(
                    "Restore changed items",
                    f"Unknown changed item area: {area_name}",
                    parent=window,
                )
                return

            output[offset:offset + item_size] = self.base_card.data[
                offset:
                offset + item_size
            ]

        output = bytes(output)

        check_text, check_ok = cardtool.render_check_card_report(
            self.base_card.path,
            output,
        )

        if not check_ok:
            self.log("")
            self.log("Restore changed items aborted: check-card returned WARN.")
            self.log(check_text)
            messagebox.showerror(
                "Restore changed items failed",
                "The restored working destination did not pass check-card.\n\n"
                "The working destination was not changed.",
                parent=window,
            )
            return

        old_digest = cardtool.fnv1a32(self.working_data)

        if output == self.working_data:
            self.log("")
            self.log("Restore changed items skipped")
            self.log(f"  Scope:         {scope_text}")
            self.log("  Reason:        selected items already match base")
            self.log("  File written:  no")
            self.restore_changed_slots_window = None
            window.destroy()
            return

        self.working_data = output
        self.working_change_count += 1

        self.update_base_label()
        self.refresh_lists()

        self.log("")
        self.log("Restore changed items applied")
        self.log(f"  Scope:          {scope_text}")
        self.log(f"  Base file:      {self.base_card.path}")
        self.log(f"  Restored items: {len(selected_changes)}")
        self.log(f"  Old digest:     0x{old_digest:08X}")
        self.log(f"  Working digest: 0x{cardtool.fnv1a32(self.working_data):08X}")
        self.log("  check-card:     OK")
        self.log("  File written:   no")
        self.log("  Save required:  yes")

        self.restore_changed_slots_window = None
        window.destroy()

    def refresh_lists(self):
        source = self.selected_source_card()

        performance_filter = self.performance_filter_var.get()
        patch_filter = self.patch_filter_var.get()

        self._fill_tree(
            self.source_perf_tree,
            source.data if source else None,
            "performance",
            performance_filter,
        )
        self._fill_tree(
            self.source_patch_tree,
            source.data if source else None,
            "patch",
            patch_filter,
        )
        self._fill_tree(
            self.base_perf_tree,
            self.working_data,
            "performance",
            performance_filter,
        )
        self._fill_tree(
            self.base_patch_tree,
            self.working_data,
            "patch",
            patch_filter,
        )

        self.refresh_changed_slots()

    def _fill_tree(self, tree, data, area, filter_text=""):
        for item in tree.get_children():
            tree.delete(item)

        if data is None:
            return

        if area == "performance":
            rows = cardtool.collect_named_slots(
                data,
                "Performance",
                cardtool.PERFORMANCE_AREA_START,
                cardtool.PERFORMANCE_SLOT_SIZE,
                cardtool.PERFORMANCE_NAME_BYTES,
                cardtool.PERFORMANCE_SLOT_COUNT,
            )
        elif area == "patch":
            rows = cardtool.collect_named_slots(
                data,
                "Patch",
                cardtool.PATCH_AREA_START,
                cardtool.PATCH_SLOT_SIZE,
                cardtool.PATCH_NAME_BYTES,
                cardtool.PATCH_SLOT_COUNT,
            )
        else:
            raise ValueError(f"unknown area: {area}")

        normalized_filter = filter_text.strip().lower()

        for _area_name, slot, offset, name, digest in rows:
            state = self.slot_state_label(data, area, slot)

            is_working_tree = (
                tree == self.base_perf_tree or
                tree == self.base_patch_tree
            )

            if not is_working_tree and state == "?":
                state = ""

            changed = False

            if is_working_tree:
                changed = self.working_slot_is_changed_from_base(area, slot)

                if changed:
                    if state == "INITIAL":
                        state = "INITIAL+CHANGED"
                    else:
                        state = "CHANGED"

            if normalized_filter:
                searchable_text = f"{slot:03d} {name} {state}".lower()

                if normalized_filter not in searchable_text:
                    continue

            tags = ()

            if changed:
                tags = ("changed",)
            elif state == "INITIAL":
                tags = ("initial",)

            tree.insert(
                "",
                tk.END,
                iid=str(slot),
                values=(
                    f"{slot:03d}",
                    name,
                    state,
                    f"0x{offset:04X}",
                    f"0x{digest:08X}",
                ),
                tags=tags,
            )

    def clear_status(self):
        self.status_text.configure(state=tk.NORMAL)
        self.status_text.delete("1.0", tk.END)
        self.status_text.configure(state=tk.DISABLED)

        self.log("Status cleared.")

    def copy_status_to_clipboard(self):
        text = self.status_text.get("1.0", tk.END).rstrip()

        if text == "":
            self.log("Copy status skipped: status is empty.")
            return

        self.clipboard_clear()
        self.clipboard_append(text)
        self.log("Status copied to clipboard.")

    def log(self, text):
        _first, last = self.status_text.yview()
        should_autoscroll = last >= 0.999

        self.status_text.configure(state=tk.NORMAL)

        lower_text = text.lower()
        pos = 0

        while pos < len(text):
            warning_pos = lower_text.find("warning", pos)
            warn_pos = lower_text.find("warn", pos)

            candidates = [p for p in (warning_pos, warn_pos) if p >= 0]

            if not candidates:
                self.status_text.insert(tk.END, text[pos:])
                break

            next_pos = min(candidates)

            if next_pos > pos:
                self.status_text.insert(tk.END, text[pos:next_pos])

            if lower_text.startswith("warning", next_pos):
                marker_len = len("warning")
            else:
                marker_len = len("warn")

            self.status_text.insert(
                tk.END,
                text[next_pos:next_pos + marker_len],
                "warning",
            )

            pos = next_pos + marker_len

        self.status_text.insert(tk.END, "\n")

        if should_autoscroll:
            self.status_text.see(tk.END)

        self.status_text.configure(state=tk.DISABLED)


def main():
    app = MiniJV880CardRamGui()
    app.mainloop()


if __name__ == "__main__":
    main()
