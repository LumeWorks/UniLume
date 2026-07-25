#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Update Arch Linux repository metadata.
# Requires: repo-add, gpg
# Usage: update-arch-repo.sh <channel>

set -euo pipefail

CHANNEL="${1:?Usage: $0 <channel>}"
BASE_DIR="${ARCH_REPO_DIR:-dist/repos/arch}/${CHANNEL}"
SIGNING_KEY="${UNILUME_SIGNING_KEY:-unilume@dismon.me}"

if [[ "$CHANNEL" != "stable" && "$CHANNEL" != "testing" ]]; then
  echo "Error: channel must be 'stable' or 'testing'"
  exit 1
fi

if ! command -v repo-add &>/dev/null; then
  echo "Error: repo-add not found (pacman-contrib)"
  exit 1
fi

echo "Updating Arch repo metadata for channel '${CHANNEL}' in ${BASE_DIR}..."

mkdir -p "${BASE_DIR}/x86_64" "${BASE_DIR}/any"

create_repo_db() {
  local arch="$1"
  local dir="${BASE_DIR}/${arch}"
  local db="${dir}/unilume.db.tar.zst"
  local files="${dir}/unilume.files.tar.zst"

  # Collect packages for this arch
  local pkg_list=()
  while IFS= read -r pkg; do
    pkg_list+=("$pkg")
  done < <(find "$dir" -name "*.pkg.tar.zst" -type f 2>/dev/null)

  if [[ ${#pkg_list[@]} -eq 0 ]]; then
    echo "  No packages for ${arch}"
    return
  fi

  echo "  Building repo database for ${arch}..."

  # Remove old database first
  rm -f "$db" "$files" "${db}.sig" "${files}.sig"

  # Create new database
  repo-add --sign --key "$SIGNING_KEY" "$db" "${pkg_list[@]}"

  if [[ -f "${db}.sig" ]]; then
    echo "  Signed database for ${arch}"
  fi
}

create_repo_db "x86_64"
create_repo_db "any"

echo "Arch repo metadata updated for '${CHANNEL}'"
