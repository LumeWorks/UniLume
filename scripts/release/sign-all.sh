#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sign all packages in a directory with the UniLume release key.
# Also generates SHA256SUMS and SHA256SUMS.asc.
# Usage: sign-all.sh <directory>

set -euo pipefail

DIR="${1:?Usage: $0 <directory>}"
SIGNATURE_EXT=".asc"

if [[ ! -d "$DIR" ]]; then
  echo "Error: directory not found: $DIR"
  exit 1
fi

echo "Signing all packages in ${DIR}..."

# Sign each deb/rpm/pkg.tar.zst/tar.zst
find "$DIR" -type f \( -name '*.deb' -o -name '*.rpm' -o -name '*.pkg.tar.zst' -o -name '*.tar.zst' \) | while read -r pkg; do
  if [[ ! -f "${pkg}${SIGNATURE_EXT}" ]]; then
    echo "  Signing: ${pkg}"
    scripts/release/sign-package.sh "$pkg"
  else
    echo "  Already signed: ${pkg}"
  fi
done

# Generate checksums
echo "Generating SHA256SUMS..."
cd "$DIR"
sha256sum ./*.deb ./*.rpm ./*.pkg.tar.zst ./*.tar.zst 2>/dev/null > SHA256SUMS
gpg --batch --yes --clearsign \
  --default-key "${UNILUME_SIGNING_KEY:-unilume@dismon.me}" \
  --output SHA256SUMS.asc \
  SHA256SUMS
rm -f SHA256SUMS
echo "Created SHA256SUMS.asc"
