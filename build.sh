#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/out/build"
CACHE_DIR="${ROOT_DIR}/out/cache/third_party"
CMAKE_BIN="${CMAKE:-cmake}"
GENERATOR="${CMAKE_GENERATOR:-Ninja}"
BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"
ENABLE_LTO="${ENABLE_LTO:-OFF}"

mkdir -p "${CACHE_DIR}"

need_configure=1
if [[ -f "${BUILD_DIR}/CMakeCache.txt" ]]; then
    need_configure=0
    cache_file="${BUILD_DIR}/CMakeCache.txt"
    if ! grep -q "^CMAKE_BUILD_TYPE:STRING=${BUILD_TYPE}$" "${cache_file}" 2>/dev/null; then
        need_configure=1
    fi
    if ! grep -q "^ENABLE_LTO:BOOL=${ENABLE_LTO}$" "${cache_file}" 2>/dev/null; then
        need_configure=1
    fi
fi

if [[ "${need_configure}" -eq 1 ]]; then
    "${CMAKE_BIN}" \
        -S "${ROOT_DIR}" \
        -B "${BUILD_DIR}" \
        -G "${GENERATOR}" \
        -D CMAKE_BUILD_TYPE="${BUILD_TYPE}" \
        -D ENABLE_LTO="${ENABLE_LTO}"
fi

"${CMAKE_BIN}" --build "${BUILD_DIR}" --parallel
