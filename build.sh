#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CMAKE_BIN="${CMAKE:-cmake}"
CACHE_DIR="${ROOT_DIR}/out/cache"
BUILD_DIR="${ROOT_DIR}/out/build"
PRESET="release-lto-on"

mkdir -p "${CACHE_DIR}"

CACHE_FILE="${BUILD_DIR}/CMakeCache.txt"
PRESET_FILE="${BUILD_DIR}/.envy-preset"

need_configure=1
if [[ -f "${CACHE_FILE}" && -f "${PRESET_FILE}" ]]; then
  if [[ "${PRESET}" == "$(<"${PRESET_FILE}")" ]]; then
    need_configure=0
  fi
fi

if [[ "${need_configure}" -eq 1 ]]; then
  "${CMAKE_BIN}" --preset "${PRESET}" --log-level=STATUS
  mkdir -p "${BUILD_DIR}"
  printf '%s\n' "${PRESET}" > "${PRESET_FILE}"
fi

"${CMAKE_BIN}" --build --preset "${PRESET}"
