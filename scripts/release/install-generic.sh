#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Generic installer for UniLume tar.zst distribution.
# Usage: install.sh [--prefix=/usr/local] [--destdir=] [--dry-run]

set -euo pipefail

PREFIX="/usr/local"
DESTDIR=""
DRY_RUN=false

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix=*) PREFIX="${1#*=}" ;;
    --destdir=*) DESTDIR="${1#*=}" ;;
    --dry-run) DRY_RUN=true ;;
    *) echo "Unknown option: $1"; exit 1 ;;
  esac
  shift
done

ROOT="${DESTDIR}${PREFIX}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="${SCRIPT_DIR}/manifest.sha256"

if [[ ! -f "$MANIFEST" ]]; then
  echo "Error: manifest.sha256 not found in ${SCRIPT_DIR}"
  echo "Run this script from the extracted package directory."
  exit 1
fi

# Verify checksums
echo "Verifying package integrity..."
sha256sum -c "$MANIFEST" || {
  echo "Error: package integrity check failed"
  exit 1
}

echo "Installing UniLume to ${ROOT}..."

if [[ "$DRY_RUN" == true ]]; then
  echo "[dry-run] Would install:"
  while IFS= read -r line; do
    file="${line#*  }"
    [[ -z "$file" ]] && continue
    echo "  ${ROOT}/${file}"
  done < <(sed -n '/^#/d; /./p' "$MANIFEST")
  exit 0
fi

# Install files from manifest
while IFS= read -r line; do
  [[ "$line" =~ ^# ]] && continue
  [[ -z "$line" ]] && continue
  file="${line#*  }"
  src="${SCRIPT_DIR}/${file}"
  dst="${ROOT}/${file}"
  mkdir -p "$(dirname "$dst")"
  cp -a "$src" "$dst"
  echo "  ${dst}"
done < <(grep -v 'manifest.sha256' "$MANIFEST")

# Write install manifest for uninstaller
INSTALL_MANIFEST="${DESTDIR}/var/lib/unilume/installed-files.txt"
mkdir -p "$(dirname "$INSTALL_MANIFEST")"
cp "$MANIFEST" "$INSTALL_MANIFEST"

echo "Installation complete."
echo "To use UniLume, restart Fcitx5: fcitx5 -rd"
echo "Or log out and back in."
