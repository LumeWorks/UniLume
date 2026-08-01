#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later

set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_root=${UNILUME_PRODUCT_BUILD_DIR:-"${project_root}/build/product"}
fcitx_source="${project_root}/vendor/fcitx5"
fcitx_build="${build_root}/fcitx5"
fcitx_prefix="${build_root}/prefix"
unilume_build="${build_root}/unilume"

if [[ ! -f "${fcitx_source}/CMakeLists.txt" ]]; then
    echo "vendor/fcitx5 is missing; run: git submodule update --init --recursive" >&2
    exit 2
fi
"${project_root}/scripts/verify-fcitx-pin.sh"

cmake -S "${fcitx_source}" -B "${fcitx_build}" -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${fcitx_prefix}" \
    -DENABLE_TEST=ON \
    -DENABLE_WAYLAND=ON \
    -DENABLE_DBUS=ON
cmake --build "${fcitx_build}" --parallel
cmake --install "${fcitx_build}"

export PKG_CONFIG_PATH="${fcitx_prefix}/lib/pkgconfig:${fcitx_prefix}/lib64/pkgconfig:${fcitx_prefix}/share/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
system_fcitx_module_dir=$(pkg-config --variable=libdir Fcitx5Module)/fcitx5
export LD_LIBRARY_PATH="${fcitx_prefix}/lib:${fcitx_prefix}/lib64:${fcitx_prefix}/lib/fcitx5:${fcitx_prefix}/lib64/fcitx5:${system_fcitx_module_dir}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

cmake -S "${project_root}" -B "${unilume_build}" -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${fcitx_prefix}" \
    -DUNILUME_BUILD_FCITX5_ADDON=ON \
    -DBUILD_TESTING=ON
cmake --build "${unilume_build}" --parallel
ctest --test-dir "${unilume_build}" --output-on-failure

if ! rg -q '^UNILUME_HAVE_FCITX_ATOMIC_REPLACEMENT:INTERNAL=1$' \
    "${unilume_build}/CMakeCache.txt"; then
    echo "UniLume did not detect atomic Fcitx support" >&2
    exit 3
fi
