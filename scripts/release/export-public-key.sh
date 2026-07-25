#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Export the public signing key for distribution.
# Usage: export-public-key.sh [output-dir]

set -euo pipefail

OUTDIR="${1:-dist/keys}"
SIGNING_KEY="${UNILUME_SIGNING_KEY:-unilume@dismon.me}"

mkdir -p "$OUTDIR"

echo "Exporting public signing key to ${OUTDIR}..."

# ASCII-armored key for manual import
gpg --batch --armor --export "$SIGNING_KEY" > "${OUTDIR}/unilume-archive-key.asc"

# Binary key for Debian keyring
gpg --batch --export "$SIGNING_KEY" > "${OUTDIR}/unilume-archive-keyring.gpg"

echo "Created:"
echo "  ${OUTDIR}/unilume-archive-key.asc"
echo "  ${OUTDIR}/unilume-archive-keyring.gpg"
echo ""
echo "Fingerprint:"
gpg --fingerprint "$SIGNING_KEY" | grep -i fingerprint
