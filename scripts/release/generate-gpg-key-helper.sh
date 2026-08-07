#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Helper script to generate or export GPG key for GitHub Secrets.
# Usage: ./generate-gpg-key-helper.sh [key-id-or-email]

set -euo pipefail

KEY_ID="${1:-unilume@dismon.me}"

echo "Checking GPG key for: ${KEY_ID}"

if ! gpg --list-secret-keys "${KEY_ID}" &>/dev/null; then
  echo "Key not found. Generating a new batch GPG signing key for ${KEY_ID}..."
  PASSPHRASE=$(openssl rand -base64 24)
  cat <<EOF | gpg --batch --generate-key
Key-Type: RSA
Key-Length: 4096
Subkey-Type: RSA
Subkey-Length: 4096
Name-Real: UniLume Signing Key
Name-Email: ${KEY_ID}
Expire-Date: 0
Passphrase: ${PASSPHRASE}
%commit
EOF
  echo ""
  echo "Generated new key with Passphrase: ${PASSPHRASE}"
  echo "Save this passphrase for UNILUME_GPG_PASSPHRASE!"
fi

echo ""
echo "=== Base64 Private Key for UNILUME_GPG_PRIVATE_KEY ==="
gpg --batch --pinentry-mode loopback --armor --export-secret-keys "${KEY_ID}" | base64 -w0
echo ""
echo "======================================================"
