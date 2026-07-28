#!/usr/bin/env python3
"""Native GTK3 text client for exact-output Wayland qualification."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
from typing import Sequence

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import GLib, Gtk  # noqa: E402


class TextProbe:
    def __init__(self, capture: Path, state: Path, ready: Path) -> None:
        self.capture = capture
        self.state = state
        self.ready = ready
        self.window = Gtk.Window(title="UniLume Wayland GTK3 probe")
        self.window.set_default_size(640, 120)
        self.window.connect("destroy", Gtk.main_quit)

        self.entry = Gtk.Entry()
        self.entry.set_activates_default(False)
        self.entry.connect("changed", self.on_changed)
        self.entry.connect("activate", self.on_activate)
        self.window.add(self.entry)

    def on_changed(self, entry: Gtk.Entry) -> None:
        self.state.write_text(entry.get_text(), encoding="utf-8")

    def on_activate(self, entry: Gtk.Entry) -> None:
        with self.capture.open("a", encoding="utf-8", newline="") as stream:
            stream.write(entry.get_text() + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        entry.set_text("")

    def run(self) -> None:
        self.capture.write_bytes(b"")
        self.state.write_text("", encoding="utf-8")
        self.ready.unlink(missing_ok=True)
        self.window.show_all()
        self.entry.grab_focus()
        GLib.idle_add(self.mark_ready)
        Gtk.main()

    def mark_ready(self) -> bool:
        self.ready.write_text("ready\n", encoding="ascii")
        return GLib.SOURCE_REMOVE


def parse_arguments(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--state", type=Path, required=True)
    parser.add_argument("--ready", type=Path, required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    arguments = parse_arguments(argv)
    TextProbe(arguments.capture, arguments.state, arguments.ready).run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
