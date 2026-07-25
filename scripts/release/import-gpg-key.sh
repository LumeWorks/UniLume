#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Import GPG signing key into a temporary GNUPGHOME.
# Designed for CI usage. Never exports the private key material.
# Usage: import-gpg-key.sh
#
# Required env:
#   UNILUME_GPG_PRIVATE_KEY  (base64-encoded ASCII-armored private key)
#   UNILUME_GPG_PASSPHRASE

set -euo pipefail

if [[ -z "${UNILUME_GPG_PRIVATE_KEY:-}" ]]; then
  echo "Error: UNILUME_GPG_PRIVATE_KEY is not set"
  exit 1
fi

if [[ -z "${UNILUME_GPG_PASSPHRASE:-}" ]]; then
  echo "Error: UNILUME_GPG_PASSPHRASE is not set"
  exit 1
fi

# Create temporary GNUPGHOME
GNUPGHOME="$(mktemp -d)"
export GNUPGHOME
chmod 700 "$GNUPGHOME"

# Import the private key
echo "${UNILUME_GPG_PRIVATE_KEY}" | base64 -d | gpg --batch --import 2>/dev/null

# Verify import
if ! gpg --list-secret-keys --batch 2>/dev/null | grep -q 'sec'; then
  echo "Error: failed to import GPG private key"
  rm -rf "$GNUPGHOME"
  exit 1
fi

echo "GPG key imported successfully into ${GNUPGHOME}"
echo "Key fingerprint:"
gpg --fingerprint --batch 2>/dev/null | head -5
