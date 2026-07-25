#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Generic uninstaller for UniLume.
# Removes files listed in the install manifest.
# Usage: uninstall.sh [--prefix=/usr/local] [--destdir=] [--dry-run]

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
MANIFEST="${DESTDIR}/var/lib/unilume/installed-files.txt"

if [[ ! -f "$MANIFEST" ]]; then
  echo "Error: install manifest not found at ${MANIFEST}"
  echo "UniLume may have been installed via a package manager."
  exit 1
fi

echo "Removing UniLume from ${ROOT}..."

if [[ "$DRY_RUN" == true ]]; then
  echo "[dry-run] Would remove:"
fi

while IFS= read -r line; do
  [[ "$line" =~ ^# ]] && continue
  [[ -z "$line" ]] && continue
  file="${line#*  }"
  path="${ROOT}/${file}"
  if [[ "$DRY_RUN" == true ]]; then
    echo "  ${path}"
  else
    rm -f "$path"
    # Remove empty parent directories
    dir="$(dirname "$path")"
    while [[ "$dir" != "$ROOT" ]]; do
      rmdir --ignore-fail-on-non-empty "$dir" 2>/dev/null || true
      dir="$(dirname "$dir")"
    done
  fi
done < <(grep -v 'manifest.sha256' "$MANIFEST")

if [[ "$DRY_RUN" == false ]]; then
  rm -f "$MANIFEST"
  rmdir "$(dirname "$MANIFEST")" 2>/dev/null || true
  echo "UniLume has been removed."
fi

echo "Note: User config in ~/.config/fcitx5/ was not modified."
