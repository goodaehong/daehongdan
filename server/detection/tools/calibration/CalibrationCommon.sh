#!/usr/bin/env bash

set -euo pipefail

CALIBRATION_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OPENCV_DIR="$(cd -- "$CALIBRATION_DIR/.." && pwd)"
CALIBRATION_BUILD_DIR="${DHD_CALIBRATION_BUILD_DIR:-$CALIBRATION_DIR/out/build/linux-release}"

require_channel() {
    local channel="${1:-}"
    if [[ ! "$channel" =~ ^[1-4]$ ]]; then
        echo "Channel must be one of 1, 2, 3, or 4." >&2
        exit 2
    fi
}

resolve_calibration_source() {
    local channel="$1"
    local explicit_source="${2:-}"
    if [[ -n "$explicit_source" ]]; then
        printf '%s\n' "$explicit_source"
        return
    fi

    # Keep this default identical to server_main.cpp. Override the template when
    # the production server uses another MediaMTX path, for example cam{channel}det.
    local template="${DHD_CALIBRATION_SOURCE_TEMPLATE:-}"
    if [[ -z "$template" ]]; then
        template='rtsp://127.0.0.1:8554/cam{channel}'
    fi
    local zero_based=$((channel - 1))
    template="${template//\{channel\}/$channel}"
    template="${template//\{index\}/$zero_based}"
    printf '%s\n' "$template"
}

ensure_calibration_tools() {
    "$CALIBRATION_DIR/BuildCalibrationTools.sh"
}
