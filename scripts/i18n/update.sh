#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu
LC_ALL=C.UTF-8
export LC_ALL

root=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)

xgettext \
  --language=C++ \
  --keyword=_ \
  --keyword=N_ \
  --from-code=UTF-8 \
  --no-location \
  --omit-header \
  --package-name=UniLume \
  --package-version=0.1.0 \
  --msgid-bugs-address=https://github.com/dismonjames/UniLume/issues \
  --output="$root/po/unilume.pot" \
  "$root/src/config_gui/main.cpp" \
  "$root/src/config_gui/main_window.cpp" \
  "$root/src/config_gui/settings_editor.cpp" \
  "$root/src/config_gui/settings_model.cpp" \
  "$root/src/fcitx5/addon.cpp" \
  "$root/src/fcitx5/emoji_picker.cpp" \
  "$root/src/fcitx5/input_method_config.h" \
  "$root/src/fcitx5/status_action_model.cpp"

msgmerge --quiet --update --backup=none \
  "$root/po/vi.po" "$root/po/unilume.pot"
msgfmt --check --check-format --output-file=/dev/null "$root/po/vi.po"
