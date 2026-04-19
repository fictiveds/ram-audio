#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="${BUILD_DIR:-${ROOT_DIR}/build/linux-${BUILD_TYPE,,}}"
OUT_DIR="${OUT_DIR:-${ROOT_DIR}/dist/linux}"
RUN_TESTS="${RUN_TESTS:-1}"

if command -v nproc >/dev/null 2>&1; then
  JOBS="${JOBS:-$(nproc)}"
else
  JOBS="${JOBS:-4}"
fi

echo "[linux] root: ${ROOT_DIR}"
echo "[linux] build dir: ${BUILD_DIR}"
echo "[linux] out dir: ${OUT_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

if [[ "${RUN_TESTS}" != "0" ]]; then
  ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi

mkdir -p "${OUT_DIR}"
cp -f "${BUILD_DIR}/ram_audio" "${OUT_DIR}/ram_audio"
if [[ -f "${BUILD_DIR}/ram_audio_telemetry_test" ]]; then
  cp -f "${BUILD_DIR}/ram_audio_telemetry_test" "${OUT_DIR}/ram_audio_telemetry_test"
fi

chmod +x "${OUT_DIR}/ram_audio"
if [[ -f "${OUT_DIR}/ram_audio_telemetry_test" ]]; then
  chmod +x "${OUT_DIR}/ram_audio_telemetry_test"
fi

echo "[linux] done"
echo "[linux] binary: ${OUT_DIR}/ram_audio"
