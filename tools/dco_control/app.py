#!/usr/bin/env python3
"""Bench controller for the DCO board.

Drives the DCO's whole parameter surface over its USB serial port, so the board can be
tested with no Input board and no Screen attached. Notes are not handled here: the DCO
already enumerates as a USB MIDI device, so play it from a MIDI keyboard or VMPK.

Requires the firmware to be built with ENABLE_USB_CONTROL (see DCO/DCO.ino).

    python3 app.py [--port /dev/ttyACM0] [--theme dark|light]
"""

from __future__ import annotations

import argparse
import queue
import sys
import threading
import tkinter as tk
from tkinter import ttk

import params
import protocol
import theme

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  pacman -S python-pyserial   (or pip install pyserial)")

# Coalesce slider drags into one write per interval, so dragging cannot flood the link.
SEND_INTERVAL_MS = 20

# Ranges at or below this width get a spinbox instead of a slider, where one step matters.
SPINBOX_MAX_RANGE = 24


def ivar(var: tk.Variable) -> int:
    """Read a Tk variable as an int.

    ttk.Scale always writes floats, so slider variables are DoubleVar and have to be
    rounded on the way out rather than read as IntVar (which would raise on "3.5").
    """
    try:
        return int(round(float(var.get())))
    except (tk.TclError, ValueError):
        return 0


class Link:
    """Serial connection plus a background reader for the board's debug output."""

    def __init__(self) -> None:
        self.port = None
        self.rx: queue.Queue[str] = queue.Queue()
        self._reader: threading.Thread | None = None
        self._stop = threading.Event()

    @property
    def is_open(self) -> bool:
        return self.port is not None and self.port.is_open

    def open(self, device: str) -> None:
        self.close()
        self.port = serial.Serial(device, protocol.BAUD, timeout=0.1)
        self._stop.clear()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def close(self) -> None:
        self._stop.set()
        if self._reader is not None:
            self._reader.join(timeout=0.5)
            self._reader = None
        if self.port is not None:
            try:
                self.port.close()
            except OSError:
                pass
            self.port = None

    def send(self, frame: bytes) -> None:
        if not self.is_open:
            return
        try:
            self.port.write(frame)
        except OSError as exc:
            self.rx.put(f"[link] write failed: {exc}\n")
            self.close()

    def _read_loop(self) -> None:
        # The board only ever sends plain debug text back, never frames, so decode
        # loosely and forward whatever arrives to the log pane.
        while not self._stop.is_set():
            try:
                data = self.port.read(256)
            except (OSError, AttributeError, TypeError):
                break
            if data:
                self.rx.put(data.decode("utf-8", errors="replace"))


class App:
    def __init__(self, root: tk.Tk, preferred_port: str | None, mode: str = "dark") -> None:
        self.root = root
        self.link = Link()
        self.pending: dict[str, bytes] = {}  # dedup key -> most recent frame
        self.param_vars: dict[int, tk.Variable] = {}
        self.block_vars: dict[str, dict[str, tk.Variable]] = {}
        self.blocks_by_key = {b.key: b for b in params.BLOCKS}
        self.mode = mode if mode in theme.MODES else "dark"
        self.dot_on = False

        root.title("DCO bench controller")
        # Wide enough that the toolbar's right-hand side is never squeezed out by pack().
        root.geometry("1140x840")
        root.minsize(900, 600)
        # Before building, so every widget is created already styled.
        theme.apply(root, self.mode)

        self._build_toolbar(preferred_port)
        self._build_tabs()
        self._build_log()
        # The canvases and log pane are plain Tk and were created after apply()'s own pass.
        theme.retint(root)
        self._style_log_tags()

        self.root.after(SEND_INTERVAL_MS, self._flush)
        self.root.after(50, self._drain_log)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # --- toolbar ---------------------------------------------------------

    def _build_toolbar(self, preferred_port: str | None) -> None:
        bar = ttk.Frame(self.root, padding=(8, 6))
        bar.pack(fill="x")

        ttk.Label(bar, text="Port").pack(side="left")
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(bar, textvariable=self.port_var, width=28, state="readonly")
        self.port_combo.pack(side="left", padx=6)
        self._refresh_ports(preferred_port)

        ttk.Button(bar, text="Rescan", command=lambda: self._refresh_ports(None)).pack(side="left")
        self.connect_btn = ttk.Button(bar, text="Connect", command=self._toggle_connect)
        self.connect_btn.pack(side="left", padx=6)

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=10)
        ttk.Button(bar, text="Send all", command=self.send_all,
                   style="Accent.TButton").pack(side="left")
        ttk.Button(bar, text="Reset to defaults", command=self.reset_defaults).pack(side="left", padx=6)

        self.status_var = tk.StringVar(value="not connected")
        ttk.Label(bar, textvariable=self.status_var, style="Status.TLabel").pack(side="right")
        self.status_dot = ttk.Label(bar, text="\u25cf", style="Dot.TLabel")
        self.status_dot.pack(side="right", padx=(10, 5))

        self.theme_btn = ttk.Button(bar, command=self._toggle_theme, width=7)
        self.theme_btn.pack(side="right", padx=(0, 12))
        self._sync_theme_button()

    def _toggle_theme(self) -> None:
        self.mode = "light" if self.mode == "dark" else "dark"
        # ttk resolves styles at draw time, so this restyles the live widget tree in place.
        theme.apply(self.root, self.mode)
        self._style_log_tags()
        self._sync_theme_button()
        self._set_status_dot(self.link.is_open)

    def _sync_theme_button(self) -> None:
        self.theme_btn.config(text="Light" if self.mode == "dark" else "Dark")

    def _set_status_dot(self, connected: bool) -> None:
        self.status_dot.config(style="DotOn.TLabel" if connected else "Dot.TLabel")
        self.dot_on = connected

    def _refresh_ports(self, preferred: str | None) -> None:
        values = [f"{p.device}  ({p.description})" for p in protocol.find_dco_ports()]
        if preferred:
            values.insert(0, preferred)
        self.port_combo["values"] = values
        if values:
            self.port_combo.current(0)
        else:
            self.port_var.set("")

    def _selected_device(self) -> str:
        raw = self.port_var.get().strip()
        return raw.split()[0] if raw else ""

    def _toggle_connect(self) -> None:
        if self.link.is_open:
            self.link.close()
            self.connect_btn.config(text="Connect")
            self.status_var.set("not connected")
            self._set_status_dot(False)
            self.log("[link] disconnected\n")
            return
        device = self._selected_device()
        if not device:
            self.log("[link] no serial port selected\n")
            return
        try:
            self.link.open(device)
        except (OSError, ValueError) as exc:
            self.log(f"[link] could not open {device}: {exc}\n")
            return
        self.connect_btn.config(text="Disconnect")
        self.status_var.set(f"connected to {device}")
        self._set_status_dot(True)
        self.log(f"[link] connected to {device} -- press 'Send all' to push this window's state\n")

    # --- tabs ------------------------------------------------------------

    def _build_tabs(self) -> None:
        notebook = ttk.Notebook(self.root)
        notebook.pack(fill="both", expand=True, padx=8, pady=4)

        for group in params.GROUP_ORDER:
            page = ttk.Frame(notebook, padding=8)
            notebook.add(page, text=group)
            inner = self._scrollable(page)
            row = 0
            for block in [b for b in params.BLOCKS if b.group == group]:
                row = self._add_block(inner, block, row)
            for param in [p for p in params.PARAMS if p.group == group]:
                row = self._add_param(inner, param, row)
            if group == params.GROUP_SYNC:
                row = self._add_diagnostics(inner, row)
            inner.columnconfigure(1, weight=1)

    def _scrollable(self, parent: ttk.Frame) -> ttk.Frame:
        canvas = tk.Canvas(parent, highlightthickness=0)
        bar = ttk.Scrollbar(parent, orient="vertical", command=canvas.yview)
        inner = ttk.Frame(canvas)
        canvas.configure(yscrollcommand=bar.set)
        canvas.pack(side="left", fill="both", expand=True)
        bar.pack(side="right", fill="y")
        window = canvas.create_window((0, 0), window=inner, anchor="nw")

        def on_configure(_event=None):
            canvas.configure(scrollregion=canvas.bbox("all"))
            canvas.itemconfigure(window, width=canvas.winfo_width())

        inner.bind("<Configure>", on_configure)
        canvas.bind("<Configure>", on_configure)

        # Bind the wheel only while the pointer is over this page. Using bind_all
        # unconditionally would make every tab scroll whichever canvas was built last.
        def scroll(event):
            canvas.yview_scroll(-2 if event.num == 4 else 2, "units")

        def grab(_e):
            canvas.bind_all("<Button-4>", scroll)
            canvas.bind_all("<Button-5>", scroll)

        def release(_e):
            canvas.unbind_all("<Button-4>")
            canvas.unbind_all("<Button-5>")

        parent.bind("<Enter>", grab)
        parent.bind("<Leave>", release)
        return inner

    def _add_param(self, parent: ttk.Frame, p: params.Param, row: int) -> int:
        label = ttk.Label(parent, text=f"{p.label}  [{p.pid}]")
        label.grid(row=row, column=0, sticky="w", pady=4, padx=(0, 10))
        if p.note:
            self._tooltip(label, p.note)

        if p.kind == "slider":
            var = tk.DoubleVar(value=p.default)
            self.param_vars[p.pid] = var
            readout = ttk.Label(parent, width=6, text=str(p.default), anchor="e",
                                style="Readout.TLabel")

            if p.hi - p.lo <= SPINBOX_MAX_RANGE:
                def on_spin(pid=p.pid, var=var, rd=readout):
                    rd.config(text=str(ivar(var)))
                    self.queue_param(pid, ivar(var))

                ttk.Spinbox(parent, from_=p.lo, to=p.hi, textvariable=var, width=8,
                            command=on_spin).grid(row=row, column=1, sticky="w", pady=4)
                readout.grid_forget()
            else:
                def on_slide(_v, pid=p.pid, var=var, rd=readout):
                    rd.config(text=str(ivar(var)))
                    self.queue_param(pid, ivar(var))

                ttk.Scale(parent, from_=p.lo, to=p.hi, variable=var, orient="horizontal",
                          command=on_slide).grid(row=row, column=1, sticky="ew", pady=4)
                readout.grid(row=row, column=2, sticky="e", padx=(8, 0))

        elif p.kind == "combo":
            labels = [c[0] for c in p.choices]
            lookup = dict(p.choices)
            var = tk.StringVar(value=next((c[0] for c in p.choices if c[1] == p.default), labels[0]))
            self.param_vars[p.pid] = var
            combo = ttk.Combobox(parent, textvariable=var, values=labels, state="readonly")
            combo.grid(row=row, column=1, sticky="ew", pady=4)

            def on_pick(_e=None, pid=p.pid, var=var, lookup=lookup):
                self.queue_param(pid, lookup[var.get()])

            combo.bind("<<ComboboxSelected>>", on_pick)

        elif p.kind == "check":
            var = tk.IntVar(value=p.default)
            self.param_vars[p.pid] = var

            def on_check(pid=p.pid, var=var):
                self.queue_param(pid, ivar(var))

            ttk.Checkbutton(parent, variable=var, command=on_check,
                            style=theme.check_style()).grid(
                row=row, column=1, sticky="w", pady=4)

        elif p.kind == "pulse":
            def on_pulse(pid=p.pid, value=p.pulse_value, label=p.label):
                self.send_now(protocol.param16(pid, value))
                self.log(f"[send] {label} (param {pid} = {value})\n")

            ttk.Button(parent, text="Send", command=on_pulse).grid(
                row=row, column=1, sticky="w", pady=4)

        return row + 1

    def _add_block(self, parent: ttk.Frame, block: params.Block, row: int) -> int:
        frame = ttk.LabelFrame(parent, text=block.label, padding=8)
        frame.grid(row=row, column=0, columnspan=3, sticky="ew", pady=(4, 10))
        frame.columnconfigure(1, weight=1)

        self.block_vars[block.key] = {}
        for i, f in enumerate(block.fields):
            ttk.Label(frame, text=f.label).grid(row=i, column=0, sticky="w", padx=(0, 10), pady=2)
            var = tk.DoubleVar(value=f.default)
            self.block_vars[block.key][f.key] = var
            readout = ttk.Label(frame, width=6, text=str(f.default), anchor="e",
                                style="Readout.TLabel")

            def on_slide(_v, key=block.key, var=var, rd=readout):
                rd.config(text=str(ivar(var)))
                self.queue_block(key)

            ttk.Scale(frame, from_=f.lo, to=f.hi, variable=var, orient="horizontal",
                      command=on_slide).grid(row=i, column=1, sticky="ew", pady=2)
            readout.grid(row=i, column=2, sticky="e", padx=(8, 0))

        if block.note:
            ttk.Label(frame, text=block.note, wraplength=700, style="Muted.TLabel").grid(
                row=len(block.fields), column=0, columnspan=3, sticky="w", pady=(6, 0))
        return row + 1

    def _add_diagnostics(self, parent: ttk.Frame, row: int) -> int:
        frame = ttk.LabelFrame(parent, text="Diagnostics", padding=8)
        frame.grid(row=row, column=0, columnspan=3, sticky="ew", pady=(10, 4))
        for i, (label, value) in enumerate(params.DEBUG_COMMANDS):
            def on_click(value=value, label=label):
                self.send_now(protocol.param16(params.DEBUG_PARAM_ID, value))
                self.log(f"[send] {label}\n")

            ttk.Button(frame, text=label, command=on_click).grid(row=0, column=i, padx=4)
        ttk.Label(
            frame,
            text="Output appears in the log below. The period probes only hold while no "
                 "note is playing, because voice_task() pushes a fresh divider every frame "
                 "for a held note.",
            wraplength=760,
            style="Muted.TLabel",
        ).grid(row=1, column=0, columnspan=len(params.DEBUG_COMMANDS), sticky="w", pady=(6, 0))
        return row + 1

    def _tooltip(self, widget: tk.Widget, text: str) -> None:
        state: dict[str, tk.Toplevel | None] = {"win": None}

        def show(_e):
            if state["win"] is not None:
                return
            # Created on hover, so the startup retint() cannot reach it: colour it here.
            p = theme.palette()
            win = tk.Toplevel(widget)
            win.wm_overrideredirect(True)
            win.wm_geometry(f"+{widget.winfo_rootx() + 20}+{widget.winfo_rooty() + widget.winfo_height() + 4}")
            win.configure(background=p["border"])
            tk.Label(win, text=text, justify="left", wraplength=420, padx=8, pady=5,
                     background=p["surface"], foreground=p["fg"],
                     relief="flat", borderwidth=0).pack(padx=1, pady=1)
            state["win"] = win

        def hide(_e):
            if state["win"] is not None:
                state["win"].destroy()
                state["win"] = None

        widget.bind("<Enter>", show)
        widget.bind("<Leave>", hide)

    # --- log -------------------------------------------------------------

    def _build_log(self) -> None:
        frame = ttk.LabelFrame(self.root, text="Board output", padding=6)
        frame.pack(fill="both", expand=False, padx=8, pady=(0, 8))
        self.log_text = tk.Text(frame, height=11, wrap="none")
        bar = ttk.Scrollbar(frame, orient="vertical", command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=bar.set)
        self.log_text.grid(row=0, column=0, sticky="nsew")
        bar.grid(row=0, column=1, sticky="ns")
        ttk.Button(frame, text="Clear", command=lambda: self.log_text.delete("1.0", "end")).grid(
            row=1, column=0, columnspan=2, sticky="e", pady=(4, 0))
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(0, weight=1)

    def _style_log_tags(self) -> None:
        p = theme.palette()
        self.log_text.tag_configure("link", foreground=p["accent"])
        self.log_text.tag_configure("send", foreground=p["muted"])
        self.log_text.tag_configure("ui", foreground=p["muted"])

    def log(self, text: str) -> None:
        # Tag our own lines so they read as commentary, leaving board output plain.
        tag = ""
        for name in ("link", "send", "ui"):
            if text.startswith(f"[{name}]"):
                tag = name
                break
        self.log_text.insert("end", text, tag or ())
        # Keep the buffer bounded; DCO_DEBUG_REPORT is chatty.
        if int(self.log_text.index("end-1c").split(".")[0]) > 2000:
            self.log_text.delete("1.0", "500.0")
        self.log_text.see("end")

    def _drain_log(self) -> None:
        try:
            while True:
                self.log(self.link.rx.get_nowait())
        except queue.Empty:
            pass
        if self.dot_on and not self.link.is_open:
            # A failed write closes the link from the reader thread's side.
            self._set_status_dot(False)
            self.status_var.set("not connected")
            self.connect_btn.config(text="Connect")
        self.root.after(50, self._drain_log)

    # --- sending ---------------------------------------------------------

    def queue_param(self, pid: int, value: int) -> None:
        """Stage a parameter frame, replacing any earlier unsent value for the same id."""
        self.pending[f"p{pid}"] = protocol.param16(pid, int(value))

    def queue_block(self, key: str) -> None:
        block = self.blocks_by_key[key]
        values = {k: ivar(v) for k, v in self.block_vars[key].items()}
        self.pending[f"b{key}"] = block.builder(values)

    def send_now(self, frame: bytes) -> None:
        if not self.link.is_open:
            self.log("[link] not connected\n")
            return
        self.link.send(frame)

    def _flush(self) -> None:
        if self.pending and self.link.is_open:
            self.link.send(b"".join(self.pending.values()))
        self.pending.clear()
        self.root.after(SEND_INTERVAL_MS, self._flush)

    def send_all(self) -> None:
        """Push every control once.

        Needed after connecting: the board boots with its own defaults and has no idea
        what this window is showing.
        """
        if not self.link.is_open:
            self.log("[link] not connected\n")
            return
        for p in params.PARAMS:
            if p.kind == "pulse":
                continue  # command-style; firing these would kick off autotune
            self.queue_param(p.pid, self._param_value(p))
        for block in params.BLOCKS:
            self.queue_block(block.key)
        self.log(f"[send] all {len(self.pending)} frames\n")

    def _param_value(self, p: params.Param) -> int:
        var = self.param_vars.get(p.pid)
        if var is None:
            return p.default
        if p.kind == "combo":
            return dict(p.choices)[var.get()]
        return ivar(var)

    def reset_defaults(self) -> None:
        for p in params.PARAMS:
            var = self.param_vars.get(p.pid)
            if var is None:
                continue
            if p.kind == "combo":
                var.set(next(c[0] for c in p.choices if c[1] == p.default))
            else:
                var.set(p.default)
        for block in params.BLOCKS:
            for f in block.fields:
                self.block_vars[block.key][f.key].set(f.default)
        self.log("[ui] controls reset to defaults -- press 'Send all' to push them\n")

    def _on_close(self) -> None:
        self.link.close()
        self.root.destroy()


def main() -> None:
    ap = argparse.ArgumentParser(description="DCO bench controller")
    ap.add_argument("--port", help="serial device, e.g. /dev/ttyACM0 (default: auto-detect)")
    ap.add_argument("--theme", choices=theme.MODES, default="dark",
                    help="colour scheme; also switchable from the toolbar (default: dark)")
    args = ap.parse_args()

    root = tk.Tk()
    App(root, args.port, args.theme)
    root.mainloop()


if __name__ == "__main__":
    main()
