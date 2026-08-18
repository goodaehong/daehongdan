#!/usr/bin/env bash
set -euo pipefail
root_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$root_dir/server/opencv/calibration/RunCameraCalibration.sh" 4 "$@"
