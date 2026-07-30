#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sign a package file with the UniLume release key.
# Usage: sign-package.sh <file>

set -euo pipefail

FILE="${1:?Usage: $0 <file>}"
SIGNING_KEY="${UNILUME_SIGNING_KEY:-}"
if [[ -z "$SIGNING_KEY" ]]; then
  SIGNING_KEY="$(gpg --list-secret-keys --with-colons 2>/dev/null | awk -F: '$1=="sec" {print $5; exit}')" || true
fi
SIGNING_KEY="${SIGNING_KEY:-unilume@dismon.me}"

if [[ ! -f "$FILE" ]]; then
  echo "Error: file not found: $FILE"
  exit 1
fi

if [[ -z "${GNUPGHOME:-}" ]]; then
  if [[ -d "${HOME}/.gnupg" ]] || gpg --list-secret-keys &>/dev/null; then
    export GNUPGHOME="${HOME}/.gnupg"
  else
    echo "Error: GNUPGHOME not set and no default key found"
    exit 1
  fi
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
