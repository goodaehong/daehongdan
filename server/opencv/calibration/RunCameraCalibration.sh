#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=CalibrationCommon.sh
source "$script_dir/CalibrationCommon.sh"

channel="${1:-}"
source_override="${2:-}"
require_channel "$channel"

if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "ChArUco calibration needs a graphical display for its preview window." >&2
    echo "Run it on the Raspberry Pi desktop or connect with X11 forwarding." >&2
    exit 3
fi

ensure_calibration_tools
executable="$CALIBRATION_BUILD_DIR/charuco_calibrator"
output="$OPENCV_DIR/camera_calibration_ch${channel}.yml"
source_url="$(resolve_calibration_source "$channel" "$source_override")"

echo "Channel $channel ChArUco lens calibration"
echo "Input (must match the production server stream): $source_url"
echo "Board: DICT_4X4_50, 7x5, square 50 mm, marker 35 mm"
echo "SPACE=capture, U=undo, C=finish/save, Q=quit"
echo "Move and tilt the board; collect 20-25 diverse views."

"$executable" "$source_url" --channel "$channel" --output "$output"
echo "Saved: $output"
