#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Upload package binaries to Cloudflare R2.
# Uses rclone with S3-compatible API or AWS CLI.
# Usage: upload-r2.sh <channel> <local-path> <r2-path>
#
# Example:
#   upload-r2.sh testing ./build/unilume_0.1.0~rc1_amd64.deb testing/deb/amd64/

set -euo pipefail

CHANNEL="${1:?Usage: $0 <channel> <local-path> <r2-path>}"
LOCAL_PATH="${2:?}"
R2_PATH="${3:?}"

if [[ "$CHANNEL" != "stable" && "$CHANNEL" != "testing" ]]; then
  echo "Error: channel must be 'stable' or 'testing'"
  exit 1
fi

if [[ ! -f "$LOCAL_PATH" ]]; then
  echo "Error: file not found: $LOCAL_PATH"
  exit 1
fi

# Default to rclone; fall back to aws-cli
if command -v rclone &>/dev/null; then
  CMD=(rclone copyto)
  DEST=":s3:unilume-packages/${CHANNEL}/${R2_PATH}"
elif command -v aws &>/dev/null; then
  CMD=(aws s3 cp)
  DEST="s3://unilume-packages/${CHANNEL}/${R2_PATH}"
else
  echo "Error: install rclone or aws-cli with S3-compatible config"
  exit 1
fi

echo "Uploading ${LOCAL_PATH} -> ${CHANNEL}/${R2_PATH}"
"${CMD[@]}" "$LOCAL_PATH" "$DEST"

# Verify upload
echo "Verifying upload..."
if command -v rclone &>/dev/null; then
  rclone check "$(dirname "$LOCAL_PATH")" ":s3:unilume-packages/${CHANNEL}/$(dirname "$R2_PATH")" \
    --include "$(basename "$LOCAL_PATH")" || {
    echo "Error: upload verification failed"
    exit 1
  }
else
  aws s3api head-object --bucket unilume-packages --key "${CHANNEL}/${R2_PATH}" >/dev/null || {
    echo "Error: upload verification failed"
    exit 1
  }
fi

echo "Upload verified: ${CHANNEL}/${R2_PATH}"
