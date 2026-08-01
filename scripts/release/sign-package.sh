#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sign a package file with the UniLume release key.
# Usage: sign-package.sh <file>

set -euo pipefail

FILE="${1:?Usage: $0 <file>}"
if [[ ! -f "$FILE" ]]; then
  echo "Error: file not found: $FILE"
  exit 1
fi

SIGNING_KEY="${UNILUME_SIGNING_KEY:-}"
if [[ -z "$SIGNING_KEY" ]]; then
  SIGNING_KEY="$(gpg --list-secret-keys --with-colons --batch 2>/dev/null | awk -F: '$1=="sec" {print $5; exit}')" || true
fi
if [[ -z "$SIGNING_KEY" ]]; then
  echo "Skipping GPG signature for ${FILE}: no secret key in GPG keyring"
  exit 0
fi
PASSPHRASE_FLAGS=()
if [[ -n "${UNILUME_GPG_PASSPHRASE:-}" ]]; then
  PASSPHRASE_FLAGS=(--pinentry-mode loopback --passphrase "${UNILUME_GPG_PASSPHRASE}")
fi

# Create detached signature
gpg --batch --yes "${PASSPHRASE_FLAGS[@]}" --detach-sign \
  --default-key "$SIGNING_KEY" \
  --armor \
  --output "${FILE}.asc" \
  "$FILE"

echo "Signed ${FILE} -> ${FILE}.asc"
