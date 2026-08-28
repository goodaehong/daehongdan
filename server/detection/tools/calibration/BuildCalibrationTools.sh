#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${DHD_CALIBRATION_BUILD_DIR:-$script_dir/out/build/linux-release}"
build_jobs="${DHD_BUILD_JOBS:-}"
if [[ -z "$build_jobs" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        build_jobs="$(nproc)"
    else
        build_jobs=2
    fi
fi

command -v cmake >/dev/null 2>&1 || {
    echo "cmake is required. Install cmake, g++, and the OpenCV development package." >&2
    exit 1
}

cmake -S "$script_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel "$build_jobs"

echo "Calibration tools ready: $build_dir"
