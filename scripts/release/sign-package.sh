#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sign a package file with the UniLume release key.
# Usage: sign-package.sh <file>

set -euo pipefail

FILE="${1:?Usage: $0 <file>}"
SIGNING_KEY="${UNILUME_SIGNING_KEY:-unilume@dismon.me}"

if [[ ! -f "$FILE" ]]; then
  echo "Error: file not found: $FILE"
  exit 1
fi

if [[ -z "${GNUPGHOME:-}" ]]; then
  if [[ -f "${HOME}/.gnupg/unilume-signing-key.gpg" ]]; then
    export GNUPGHOME="${HOME}/.gnupg"
  else
    echo "Error: GNUPGHOME not set and no default key found"
    exit 1
  fi
fi

# Create detached signature
gpg --batch --yes --detach-sign \
  --default-key "$SIGNING_KEY" \
  --armor \
  --output "${FILE}.asc" \
  "$FILE"

echo "Signed ${FILE} -> ${FILE}.asc"
