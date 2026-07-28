#!/usr/bin/env python3
"""Enable capability-gated direct replacement through Fcitx's real API."""

from __future__ import annotations

import gi

gi.require_version("Gio", "2.0")
from gi.repository import Gio, GLib  # noqa: E402


DESTINATION = "org.fcitx.Fcitx5"
OBJECT_PATH = "/controller"
INTERFACE = "org.fcitx.Fcitx.Controller1"
CONFIG_URI = "fcitx://config/inputmethod/unilume"


def call(
    connection: Gio.DBusConnection,
    method: str,
    parameters: GLib.Variant,
) -> GLib.Variant:
    return connection.call_sync(
        DESTINATION,
        OBJECT_PATH,
        INTERFACE,
        method,
        parameters,
        None,
        Gio.DBusCallFlags.NONE,
        -1,
        None,
    )


def read_config(connection: Gio.DBusConnection) -> dict[str, str]:
    result = call(
        connection,
        "GetConfig",
        GLib.Variant("(s)", (CONFIG_URI,)),
    )
    return result.get_child_value(0).get_variant().unpack()


def main() -> int:
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    config = read_config(connection)
    if "VerifiedDirectEnabled" not in config:
        raise SystemExit(
            "Fcitx did not expose UniLume's VerifiedDirectEnabled option"
        )
    config["VerifiedDirectEnabled"] = "True"
    raw = GLib.Variant(
        "a{sv}",
        {
            name: GLib.Variant("s", value)
            for name, value in config.items()
        },
    )
    call(
        connection,
        "SetConfig",
        GLib.Variant("(sv)", (CONFIG_URI, raw)),
    )
    observed = read_config(connection)
    if observed.get("VerifiedDirectEnabled") != "True":
        raise SystemExit("Fcitx rejected the qualification configuration")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
