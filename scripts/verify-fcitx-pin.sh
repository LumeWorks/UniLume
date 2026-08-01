#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
fcitx_source="${project_root}/vendor/fcitx5"
expected_patch=6b1f2be4a303292d3951c562ad89c99ec108423c
expected_base=7d71627695b49c06a4270f4d8106e84afb75cddb

actual_patch=$(git -C "${fcitx_source}" rev-parse HEAD)
if ! git -C "${fcitx_source}" cat-file -e "${expected_base}^{commit}" 2>/dev/null; then
    git -C "${fcitx_source}" fetch --depth=1 origin \
        refs/tags/5.1.12:refs/tags/5.1.12
fi
actual_base=$(git -C "${fcitx_source}" cat-file -p HEAD | \
    sed -n 's/^parent //p' | head -n1)
if [[ "${actual_patch}" != "${expected_patch}" ||
      "${actual_base}" != "${expected_base}" ]]; then
    echo "Pinned Fcitx source does not match the reviewed 5.1.12 backport" >&2
    echo "expected ${expected_patch} on ${expected_base}" >&2
    echo "actual   ${actual_patch} on ${actual_base}" >&2
    exit 5
fi

tag_base=$(git -C "${fcitx_source}" rev-list -n1 5.1.12)
if [[ "${tag_base}" != "${expected_base}" ]]; then
    echo "Pinned Fcitx base is not the signed 5.1.12 tag" >&2
    exit 6
fi
