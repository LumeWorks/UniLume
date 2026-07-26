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
  "$root/src/fcitx5/addon.cpp" \
  "$root/src/fcitx5/input_method_config.h" \
  "$root/src/fcitx5/status_action_model.cpp"

msgmerge --quiet --update --backup=none \
  "$root/po/vi.po" "$root/po/unilume.pot"
msgfmt --check --check-format --output-file=/dev/null "$root/po/vi.po"
