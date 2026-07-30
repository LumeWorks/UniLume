#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Run the native Wayland qualification harness inside a disposable wlroots
# session. The compositor, the D-Bus session, the Fcitx profile and the UniLume
# addon directory are all private to this run, so the harness never observes or
# mutates the operator's real desktop.
#
# This script only automates the wlroots family, because that is the family
# whose compositors implement zwp_virtual_keyboard_v1 and can therefore accept
# injected key events from inside the session. KWin and Mutter must be
# qualified interactively on a real desktop; see docs/wayland-validation.md.

set -eu

if [ "$#" -lt 1 ] || [ ! -f "$1/lib/fcitx5/unilume.so" ]; then
  echo "usage: $0 INSTALLED_PREFIX [qualify_wayland_compositor.py args...]" >&2
  exit 2
fi

prefix=$1
shift

script_directory=$(cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(cd -- "$script_directory/../.." && pwd)

for binary in sway foot wtype fcitx5 fcitx5-remote dbus-run-session; do
  if ! command -v "$binary" >/dev/null 2>&1; then
    echo "missing required qualification tool: $binary" >&2
    exit 1
  fi
done

fcitx_libdir=${FCITX_SYSTEM_LIBDIR:-$(pkg-config --variable=libdir Fcitx5Core)}
if [ -z "$fcitx_libdir" ]; then
  echo "cannot determine the Fcitx library directory" >&2
  exit 1
fi

work=$(mktemp -d)
# The disposable session is removed on exit, which would also discard the
# compositor and Fcitx logs that explain a failure. Set UNILUME_WAYLAND_LOG_DIR
# to retain copies, which is how CI keeps a failed run diagnosable.
save_logs() {
  [ -n "${UNILUME_WAYLAND_LOG_DIR:-}" ] || return 0
  mkdir -p "$UNILUME_WAYLAND_LOG_DIR" 2>/dev/null || return 0
  for name in sway.log fcitx.log; do
    [ -f "$work/$name" ] && cp "$work/$name" "$UNILUME_WAYLAND_LOG_DIR/$name"
  done
  [ -f "$work/results/client.log" ] &&
    cp "$work/results/client.log" "$UNILUME_WAYLAND_LOG_DIR/client.log"
  return 0
}

trap 'save_logs; rm -rf -- "$work"' EXIT HUP INT TERM
mkdir -p "$work/runtime" "$work/config/fcitx5" "$work/data" "$work/results"
chmod 700 "$work/runtime"

cp "$repository_root/tests/wayland/fixtures/fcitx5/profile" \
  "$work/config/fcitx5/profile"
# An empty configuration keeps the trigger key from toggling the input method
# off while the harness is injecting keys.
printf '[Hotkey]\nEnumerateWithTriggerKeys=False\n' > "$work/config/fcitx5/config"
printf 'exec true\n' > "$work/sway.conf"

# The quoted program expands only inside the isolated child shell.
# shellcheck disable=SC2016
dbus-run-session -- sh -eu -c '
  prefix=$1
  work=$2
  fcitx_libdir=$3
  repository_root=$4
  shift 4

  export XDG_RUNTIME_DIR="$work/runtime"
  export XDG_CONFIG_HOME="$work/config"
  export XDG_DATA_HOME="$work/data"
  export XDG_DATA_DIRS="$prefix/share:/usr/local/share:/usr/share"
  export FCITX_ADDON_DIRS="$prefix/lib/fcitx5:$fcitx_libdir/fcitx5"
  export XDG_SESSION_TYPE=wayland
  export XDG_CURRENT_DESKTOP=sway
  export LANG=${LANG:-C.UTF-8}
  unset DISPLAY

  # Headless provides the virtual output; libinput attaches kernel input
  # devices (including UniLume's Backspace-only /dev/uinput node) to the
  # seat. Direct-only composition (ADR 0005) replaces text by emitting
  # synthetic Backspaces through that device — without libinput on the
  # seat the client never observes the deletion/commit sequence.
  #
  # Do NOT set WLR_LIBINPUT_NO_DEVICES=1.
  export WLR_BACKENDS=headless,libinput
  export WLR_RENDERER=pixman
  export LIBINPUT_ALLOW_DEVICE=1
  sway -c "$work/sway.conf" >"$work/sway.log" 2>&1 &
  sway_pid=$!
  trap "kill $sway_pid 2>/dev/null || true" EXIT HUP INT TERM

  wayland_display=""
  for _ in $(seq 1 100); do
    for candidate in "$XDG_RUNTIME_DIR"/wayland-*; do
      # An unmatched glob stays literal, so only accept a real socket.
      [ -S "$candidate" ] || continue
      wayland_display=$(basename "$candidate")
      break
    done
    [ -n "$wayland_display" ] && break
    sleep 0.1
  done
  if [ -z "$wayland_display" ]; then
    cat "$work/sway.log" >&2
    echo "the headless compositor did not expose a Wayland socket" >&2
    exit 1
  fi
  export WAYLAND_DISPLAY="$wayland_display"

  export UNILUME_FCITX_DIAGNOSTICS=1
  export UNILUME_FCITX_DIAGNOSTIC_FILE="$work/results/unilume-diagnostic.json"
  # Keep Fcitx in the foreground process group so this script owns its
  # lifetime; a daemonized instance would survive the disposable session.
  fcitx5 -D --disable=xim,xcb,fcitx4frontend,ibusfrontend \
    >"$work/fcitx.log" 2>&1 &
  fcitx_pid=$!
  trap "kill $fcitx_pid $sway_pid 2>/dev/null || true" EXIT HUP INT TERM

  ready=0
  for _ in $(seq 1 150); do
    if grep -q "Loaded addon unilume" "$work/fcitx.log"; then
      ready=1
      break
    fi
    sleep 0.1
  done
  if [ "$ready" -ne 1 ]; then
    cat "$work/fcitx.log" >&2
    cat "$work/sway.log" >&2
    echo "the UniLume addon did not load in the isolated Fcitx instance" >&2
    exit 1
  fi
  if ! grep -q "Loaded addon waylandim" "$work/fcitx.log"; then
    cat "$work/fcitx.log" >&2
    echo "Fcitx did not load the Wayland input-method frontend" >&2
    exit 1
  fi

  status=0
  python3 -B "$repository_root/scripts/test/qualify_wayland_compositor.py" \
    --work-directory "$work/results" \
    --diagnostic-file "$UNILUME_FCITX_DIAGNOSTIC_FILE" \
    "$@" || status=$?

  exit "$status"
' sh "$prefix" "$work" "$fcitx_libdir" "$repository_root" "$@"
