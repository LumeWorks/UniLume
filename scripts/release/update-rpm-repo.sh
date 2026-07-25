#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Update RPM repository metadata.
# Usage: update-rpm-repo.sh <channel>
# Requires: createrepo_c, gpg

set -euo pipefail

CHANNEL="${1:?Usage: $0 <channel>}"
BASE_DIR="${RPM_REPO_DIR:-dist/repos/rpm}/${CHANNEL}"
SIGNING_KEY="${UNILUME_SIGNING_KEY:-unilume@dismon.me}"

if [[ "$CHANNEL" != "stable" && "$CHANNEL" != "testing" ]]; then
  echo "Error: channel must be 'stable' or 'testing'"
  exit 1
fi

if ! command -v createrepo_c &>/dev/null && ! command -v createrepo &>/dev/null; then
  echo "Error: createrepo_c not found"
  exit 1
fi

CREATEREPO=$(command -v createrepo_c || command -v createrepo)

echo "Updating RPM repo metadata for channel '${CHANNEL}' in ${BASE_DIR}..."

# Create repo structure if missing
mkdir -p "${BASE_DIR}/x86_64" "${BASE_DIR}/aarch64"

for ARCH in x86_64 aarch64; do
  ARCH_DIR="${BASE_DIR}/${ARCH}"
  if [[ -d "$ARCH_DIR" ]] && find "$ARCH_DIR" -name '*.rpm' -quit 2>/dev/null; then
    echo "  Updating ${ARCH_DIR}..."
    $CREATEREPO --update --pretty --verbose "$ARCH_DIR"
  fi
done

# Sign repomd.xml if present
for ARCH in x86_64 aarch64; do
  REPOMD="${BASE_DIR}/${ARCH}/repodata/repomd.xml"
  if [[ -f "$REPOMD" && ! -f "${REPOMD}.asc" ]]; then
    echo "  Signing repomd.xml for ${ARCH}..."
    gpg --batch --yes --detach-sign \
      --default-key "$SIGNING_KEY" \
      --armor \
      --output "${REPOMD}.asc" \
      "$REPOMD"
  fi
done

echo "RPM repo metadata updated for '${CHANNEL}'"
