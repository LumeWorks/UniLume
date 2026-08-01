#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
output_dir=${1:-"${project_root}/dist/packages/deb"}
work_dir=$(mktemp -d)
trap 'rm -rf "${work_dir}"' EXIT

if [[ ! -f "${project_root}/vendor/fcitx5/CMakeLists.txt" ]]; then
    echo "Pinned Fcitx source is missing" >&2
    exit 2
fi

mkdir -p "${output_dir}"
cd "${work_dir}"
apt-get source fcitx5=5.1.12-2
fcitx_source=$(find . -mindepth 1 -maxdepth 1 -type d -name 'fcitx5-*' -print -quit)
if [[ -z "${fcitx_source}" ]]; then
    echo "Could not locate the unpacked Fcitx Debian source" >&2
    exit 3
fi

rsync -a --delete --exclude debian/ \
    "${project_root}/vendor/fcitx5/" "${fcitx_source}/"
cd "${fcitx_source}"
dch --local +unilume1 --distribution stable \
    "Add atomic surrounding-text replacement for UniLume."
python3 - <<'PY'
from pathlib import Path

control = Path("debian/control")
text = control.read_text()
marker = "Package: libfcitx5core7\n"
start = text.index(marker)
end = text.index("\n\n", start)
stanza = text[start:end]
if "Provides: fcitx5-atomic-replacement" not in stanza:
    lines = stanza.splitlines()
    insert_at = next(
        (index for index, line in enumerate(lines) if line.startswith("Depends:")),
        len(lines),
    )
    lines.insert(insert_at, "Provides: fcitx5-atomic-replacement")
    text = text[:start] + "\n".join(lines) + text[end:]
control.write_text(text)
PY
dpkg-buildpackage -b -uc -us
cp ../*.deb "${output_dir}/"

apt-get install -y "${output_dir}"/*.deb
if ! nm -D /usr/lib/*/libFcitx5Core.so.* 2>/dev/null | \
    c++filt | grep -q 'AtomicSurroundingTextInputContext'; then
    echo "Installed Fcitx packages do not expose the atomic replacement ABI" >&2
    exit 4
fi

cd "${project_root}"
cp -a packaging/debian ./debian
trap 'rm -rf "${work_dir}" "${project_root}/debian"' EXIT
dpkg-buildpackage -b -uc -us
cp ../unilume_*.deb "${output_dir}/"

for package in "${output_dir}"/*.deb; do
    dpkg-deb --info "${package}" >/dev/null
done
sha256sum "${output_dir}"/*.deb > "${output_dir}/SHA256SUMS"
