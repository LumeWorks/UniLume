#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Qualify KWin or Mutter in a disposable native-Wayland session. Input enters
# through the compositor's own nested/remote-desktop interface; UniLume keeps
# one production Fcitx backend and never writes through XTEST or D-Bus itself.

set -eu

if [ "$#" -lt 2 ] || [ ! -f "$1/lib/fcitx5/unilume.so" ]; then
  echo "usage: $0 INSTALLED_PREFIX kwin|mutter [harness args...]" >&2
  exit 2
fi

prefix=$1
family=$2
shift 2

case "$family" in
  kwin)
    required="Xvfb xwininfo xdotool kwin_wayland fcitx5 fcitx5-remote dbus-run-session"
    ;;
  mutter)
    required="gnome-shell gdbus fcitx5 fcitx5-remote dbus-run-session python3"
    ;;
  *)
    echo "unsupported compositor family: $family" >&2
    exit 2
    ;;
esac

for binary in $required; do
  if ! command -v "$binary" >/dev/null 2>&1; then
    echo "missing required qualification tool: $binary" >&2
    exit 1
  fi
done

if [ "$family" = mutter ] &&
   ! python3 -c 'import gi; gi.require_version("Gtk", "3.0")' 2>/dev/null; then
  echo "Mutter qualification requires Python GTK3 introspection" >&2
  exit 1
fi

script_directory=$(cd -- "$(dirname -- "$0")" && pwd)
repository_root=$(cd -- "$script_directory/../.." && pwd)
fcitx_libdir=${FCITX_SYSTEM_LIBDIR:-}
if [ -z "$fcitx_libdir" ]; then
  fcitx_libdir=$(pkg-config --variable=libdir Fcitx5Core 2>/dev/null || true)
fi
if [ -z "$fcitx_libdir" ]; then
  echo "cannot determine the Fcitx library directory" >&2
  exit 1
fi

work=$(mktemp -d)
save_logs() {
  [ -n "${UNILUME_WAYLAND_LOG_DIR:-}" ] || return 0
  mkdir -p "$UNILUME_WAYLAND_LOG_DIR" 2>/dev/null || return 0
  for name in compositor.log fcitx.log; do
    [ -f "$work/$name" ] &&
      cp "$work/$name" "$UNILUME_WAYLAND_LOG_DIR/$family-$name"
  done
  [ -f "$work/results/client.log" ] &&
    cp "$work/results/client.log" \
      "$UNILUME_WAYLAND_LOG_DIR/$family-client.log"
  return 0
}
trap 'save_logs; rm -rf -- "$work"' EXIT HUP INT TERM

mkdir -p "$work/runtime" "$work/config/fcitx5" "$work/data" "$work/results"
chmod 700 "$work/runtime"
cp "$repository_root/tests/wayland/fixtures/fcitx5/profile" \
  "$work/config/fcitx5/profile"
printf '[Hotkey]\nEnumerateWithTriggerKeys=False\n' \
  > "$work/config/fcitx5/config"

# shellcheck disable=SC2016
dbus-run-session -- sh -eu -c '
  prefix=$1
  family=$2
  work=$3
  fcitx_libdir=$4
  repository_root=$5
  shift 5

  export XDG_RUNTIME_DIR="$work/runtime"
  export XDG_CONFIG_HOME="$work/config"
  export XDG_DATA_HOME="$work/data"
  export XDG_DATA_DIRS="$prefix/share:/usr/local/share:/usr/share"
  export FCITX_ADDON_DIRS="$prefix/lib/fcitx5:$fcitx_libdir/fcitx5"
  export XDG_SESSION_TYPE=wayland
  export UNILUME_FCITX_DIAGNOSTICS=1
  export UNILUME_FCITX_DIAGNOSTIC_FILE="$work/results/unilume-diagnostic.json"
  export LANG=${LANG:-C.UTF-8}

  compositor_pid=""
  fcitx_pid=""
  xvfb_pid=""
  cleanup() {
    kill $fcitx_pid $compositor_pid $xvfb_pid 2>/dev/null || true
  }
  trap cleanup EXIT HUP INT TERM

  if [ "$family" = kwin ]; then
    display_number=90
    while [ "$display_number" -le 119 ]; do
      if [ ! -S "/tmp/.X11-unix/X$display_number" ]; then
        break
      fi
      display_number=$((display_number + 1))
    done
    if [ "$display_number" -gt 119 ]; then
      echo "could not find a free nested X11 display" >&2
      exit 1
    fi
    export DISPLAY=":$display_number"
    Xvfb "$DISPLAY" -screen 0 1280x800x24 -nolisten tcp \
      >"$work/xvfb.log" 2>&1 &
    xvfb_pid=$!
    ready=0
    for _ in $(seq 1 100); do
      if xwininfo -root >/dev/null 2>&1; then
        ready=1
        break
      fi
      sleep 0.1
    done
    [ "$ready" -eq 1 ] || {
      cat "$work/xvfb.log" >&2
      echo "Xvfb did not become ready" >&2
      exit 1
    }

    socket="wayland-unilume-kwin-$$"
    kwin_wayland \
      --x11-display "$DISPLAY" \
      --socket "$socket" \
      --width 1024 \
      --height 768 \
      --no-lockscreen \
      --no-global-shortcuts \
      --inputmethod "$(command -v fcitx5)" \
      >"$work/compositor.log" 2>&1 &
    compositor_pid=$!
    export WAYLAND_DISPLAY="$socket"
    export XDG_CURRENT_DESKTOP=kwin_wayland
    export XDG_SESSION_DESKTOP=kwin_wayland

    window=""
    for _ in $(seq 1 150); do
      window=$(
        xwininfo -root -tree 2>/dev/null |
          awk "/KDE Wayland Compositor/{print \$1; exit}"
      )
      [ -n "$window" ] && [ -S "$XDG_RUNTIME_DIR/$socket" ] && break
      sleep 0.1
    done
    if [ -z "$window" ] || [ ! -S "$XDG_RUNTIME_DIR/$socket" ]; then
      cat "$work/compositor.log" >&2
      echo "KWin did not expose its host window and Wayland socket" >&2
      exit 1
    fi
    ready=0
    for _ in $(seq 1 200); do
      if grep -q "Loaded addon unilume" "$work/compositor.log"; then
        ready=1
        break
      fi
      sleep 0.1
    done
    if [ "$ready" -ne 1 ]; then
      cat "$work/compositor.log" >&2
      echo "KWin did not start the isolated Fcitx addon" >&2
      exit 1
    fi
    injector_args="--injector xdotool --xdotool-window $window"
    client_args="--client gtk3-probe"
  else
    unset DISPLAY
    export XDG_CURRENT_DESKTOP=GNOME
    export XDG_SESSION_DESKTOP=gnome-shell
    export GDK_BACKEND=wayland
    export GTK_IM_MODULE=fcitx

    gnome-shell \
      --headless \
      --wayland \
      --virtual-monitor 1024x768 \
      >"$work/compositor.log" 2>&1 &
    compositor_pid=$!
    export WAYLAND_DISPLAY=wayland-0
    ready=0
    for _ in $(seq 1 200); do
      if [ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ] &&
         gdbus introspect \
           --session \
           --dest org.gnome.Mutter.RemoteDesktop \
           --object-path /org/gnome/Mutter/RemoteDesktop \
           >/dev/null 2>&1; then
        ready=1
        break
      fi
      sleep 0.1
    done
    if [ "$ready" -ne 1 ]; then
      cat "$work/compositor.log" >&2
      echo "Mutter did not expose Wayland and RemoteDesktop" >&2
      exit 1
    fi

    fcitx5 -D --disable=waylandim,xcb,xim,fcitx4frontend,ibusfrontend \
      >"$work/fcitx.log" 2>&1 &
    fcitx_pid=$!
    injector_args="--injector mutter-remote-desktop"
    client_args="--client gtk3-probe"
  fi

  ready=0
  for _ in $(seq 1 200); do
    if fcitx5-remote >/dev/null 2>&1; then
      ready=1
      break
    fi
    sleep 0.1
  done
  if [ "$ready" -ne 1 ]; then
    [ -f "$work/fcitx.log" ] && cat "$work/fcitx.log" >&2
    cat "$work/compositor.log" >&2
    echo "Fcitx did not become ready" >&2
    exit 1
  fi

  # This uses Fcitx Controller1 GetConfig/SetConfig and verifies the accepted
  # value. It does not rely on a hand-written config file or change defaults.
  python3 -B \
    "$repository_root/scripts/test/set_fcitx_qualification_config.py"

  # The two controlled expansions contain no user data and are assembled by
  # this script, not accepted from the command line.
  # shellcheck disable=SC2086
  python3 -B "$repository_root/scripts/test/qualify_wayland_compositor.py" \
    --work-directory "$work/results" \
    --diagnostic-file "$UNILUME_FCITX_DIAGNOSTIC_FILE" \
    $injector_args \
    $client_args \
    "$@"
' sh "$prefix" "$family" "$work" "$fcitx_libdir" "$repository_root" "$@"
