#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Build generic tar.zst archive from a staged install.
# Usage: build-generic.sh <staging-dir> <version> <arch>

set -euo pipefail

STAGING="$1"
VERSION="$2"
ARCH="${3:-$(uname -m)}"

if [[ ! -d "$STAGING" ]]; then
  echo "Usage: $0 <staging-dir> <version> [arch]"
  exit 1
fi

PKGNAME="unilume-${VERSION}-linux-${ARCH}"
OUTDIR="${OUTDIR:-.}"

mkdir -p "${STAGING}/${PKGNAME}"

# Move staged files into the package directory
if [[ -d "${STAGING}/usr" ]]; then
  mv "${STAGING}/usr/"* "${STAGING}/${PKGNAME}/"
  rmdir "${STAGING}/usr"
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cp "${SCRIPT_DIR}/install-generic.sh" "${STAGING}/${PKGNAME}/install.sh"
cp "${SCRIPT_DIR}/uninstall-generic.sh" "${STAGING}/${PKGNAME}/uninstall.sh"
chmod +x "${STAGING}/${PKGNAME}/install.sh" "${STAGING}/${PKGNAME}/uninstall.sh"

# Generate install manifest
cat > "${STAGING}/${PKGNAME}/manifest.sha256" << 'MANIFEST_HEADER'
# Install manifest for UniLume
# To verify: sha256sum -c manifest.sha256
MANIFEST_HEADER

(
  cd "${STAGING}/${PKGNAME}"
  find . -type f ! -name 'manifest.sha256' | sort | while read -r f; do
    sha256sum "$f" >> manifest.sha256
  done
)

# Create the archive
cd "${STAGING}"
tar --create \
  --file="${OUTDIR}/${PKGNAME}.tar.zst" \
  --zstd \
  --sort=name \
  --owner=0:0 \
  --group=0:0 \
  --numeric-owner \
  --mtime="@${SOURCE_DATE_EPOCH:-$(date +%s)}" \
  "${PKGNAME}/"

echo "Created ${OUTDIR}/${PKGNAME}.tar.zst"
