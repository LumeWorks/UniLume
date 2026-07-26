#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

if [ "$#" -ne 1 ] || [ ! -x "$1/bin/unilume-config" ]; then
  echo "usage: $0 INSTALLED_PREFIX" >&2
  exit 2
fi

prefix=$1
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT HUP INT TERM
mkdir -p "$work/config/fcitx5" "$work/data"
cp "$(dirname "$0")/../../tests/config_gui/fixtures/fcitx5/profile" \
  "$work/config/fcitx5/profile"

fcitx_libdir=${FCITX_SYSTEM_LIBDIR:-$(pkg-config --variable=libdir Fcitx5Core)}
if [ -z "$fcitx_libdir" ]; then
  echo "cannot determine the Fcitx library directory" >&2
  exit 1
fi

# The quoted program expands only inside the isolated child shell.
# shellcheck disable=SC2016
dbus-run-session -- sh -eu -c '
  prefix=$1
  work=$2
  fcitx_libdir=$3
  export DISPLAY=:99
  unset WAYLAND_DISPLAY
  export XDG_CONFIG_HOME="$work/config"
  export XDG_DATA_HOME="$work/data"
  export XDG_DATA_DIRS="$prefix/share:/usr/local/share:/usr/share"
  export FCITX_ADDON_DIRS="$prefix/lib/fcitx5:$fcitx_libdir/fcitx5"
  export QT_QPA_PLATFORM=offscreen

  fcitx5 -D --disable=notifications,wayland,waylandim,xcb,xim \
    >"$work/fcitx.log" 2>&1 &
  fcitx_pid=$!
  trap "kill $fcitx_pid 2>/dev/null || true; \
        wait $fcitx_pid 2>/dev/null || true" EXIT HUP INT TERM

  ready=0
  for attempt in $(seq 1 100); do
    if grep -q "Loaded addon unilume" "$work/fcitx.log"; then
      ready=1
      break
    fi
    sleep 0.1
  done
  if [ "$ready" -ne 1 ]; then
    cat "$work/fcitx.log" >&2
    exit 1
  fi
  "$prefix/bin/unilume-config" --integration-smoke
' sh "$prefix" "$work" "$fcitx_libdir"
