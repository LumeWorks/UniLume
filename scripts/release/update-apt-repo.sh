#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Update APT repository metadata.
# Uses reprepro for .deb repositories.
# Usage: update-apt-repo.sh <channel>
# Requires: reprepro, gpg

set -euo pipefail

CHANNEL="${1:?Usage: $0 <channel>}"
BASE_DIR="${APT_REPO_DIR:-dist/repos/apt}/${CHANNEL}"
SIGNING_KEY="${UNILUME_SIGNING_KEY:-unilume@dismon.me}"

if [[ "$CHANNEL" != "stable" && "$CHANNEL" != "testing" ]]; then
  echo "Error: channel must be 'stable' or 'testing'"
  exit 1
fi

if ! command -v reprepro &>/dev/null; then
  echo "Error: reprepro not found (install from apt install reprepro)"
  exit 1
fi

echo "Updating APT repo metadata for channel '${CHANNEL}' in ${BASE_DIR}..."

# Create distributions config if missing
DIST_CONF="${BASE_DIR}/conf/distributions"
if [[ ! -f "$DIST_CONF" ]]; then
  mkdir -p "${BASE_DIR}/conf"
  cat > "$DIST_CONF" << DISTEOF
Origin: UniLume
Label: UniLume ${CHANNEL}
Suite: ${CHANNEL}
Codename: ${CHANNEL}
Architectures: amd64 arm64
Components: main
Description: UniLume ${CHANNEL} APT repository
SignWith: ${SIGNING_KEY}
DISTEOF
  echo "  Created distributions config"
fi

# Create options file
OPT_CONF="${BASE_DIR}/conf/options"
if [[ ! -f "$OPT_CONF" ]]; then
  cat > "$OPT_CONF" << OPTEOF
verbose
basedir ${BASE_DIR}
ask-passphrase
OPTEOF
  echo "  Created options config"
fi

# Export public key for apt-key
KEYRING="${BASE_DIR}/keyring.gpg"
if [[ ! -f "$KEYRING" ]]; then
  gpg --batch --export "$SIGNING_KEY" > "$KEYRING"
  echo "  Exported keyring"
fi

# Check for new packages to include
INCOMING="${BASE_DIR}/incoming"
if [[ -d "$INCOMING" ]]; then
  for deb in "$INCOMING"/*.deb; do
    [[ -f "$deb" ]] || continue
    echo "  Including: $(basename "$deb")"
    reprepro --conf-dir "${BASE_DIR}/conf" include "${CHANNEL}" "$deb"
    mv "$deb" "${BASE_DIR}/pool/main/"
  done
fi

# Export Release files
reprepro --conf-dir "${BASE_DIR}/conf" export "$CHANNEL"

echo "APT repo metadata updated for '${CHANNEL}'"
echo "Add to sources.list:"
echo "  deb [signed-by=${BASE_DIR}/keyring.gpg] file://${BASE_DIR} ${CHANNEL} main"
