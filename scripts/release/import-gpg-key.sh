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
#
# When GITHUB_ENV is set (GitHub Actions), GNUPGHOME and the detected key id
# are exported so later steps can sign with the imported key.

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
if ! gpg --list-secret-keys --with-colons --batch 2>/dev/null | grep -q '^sec:'; then
  echo "Error: failed to import GPG private key"
  rm -rf "$GNUPGHOME"
  exit 1
fi

SIGNING_KEY="$(gpg --list-secret-keys --with-colons --batch 2>/dev/null | awk -F: '$1=="sec" {print $5; exit}')"
if [[ -z "$SIGNING_KEY" ]]; then
  echo "Error: imported key has no secret key id"
  rm -rf "$GNUPGHOME"
  exit 1
fi

# Persist for subsequent CI steps. Without this, the next step cannot see the
# temporary keyring and signing fails with "No secret key".
if [[ -n "${GITHUB_ENV:-}" ]]; then
  {
    echo "GNUPGHOME=${GNUPGHOME}"
    echo "UNILUME_SIGNING_KEY=${SIGNING_KEY}"
  } >> "$GITHUB_ENV"
fi

echo "GPG key imported successfully into ${GNUPGHOME}"
echo "Signing key id: ${SIGNING_KEY}"
echo "Key fingerprint:"
gpg --fingerprint --batch 2>/dev/null | head -5
