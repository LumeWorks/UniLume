#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 {engine|parsers|transaction} INPUT" >&2
  exit 2
fi

target="$1"
input="$2"
case "$target" in
  engine|parsers|transaction) ;;
  *)
    echo "unknown fuzz target: $target" >&2
    exit 2
    ;;
esac

if [[ ! -f "$input" ]]; then
  echo "input does not exist: $input" >&2
  exit 2
fi

build_dir="${UNILUME_FUZZ_BUILD_DIR:-build/fuzz}"
binary="${build_dir}/fuzz/unilume_fuzz_${target}"
if [[ ! -x "$binary" ]]; then
  echo "fuzz target is not built: $binary" >&2
  exit 2
fi

ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}" \
UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}" \
  "$binary" -runs=1 "$input"
