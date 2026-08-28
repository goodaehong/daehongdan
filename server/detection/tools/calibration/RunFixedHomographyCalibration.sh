#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=CalibrationCommon.sh
source "$script_dir/CalibrationCommon.sh"

channel="${1:-}"
source_override="${2:-}"
accepted_updates="${DHD_HOMOGRAPHY_ACCEPTED_UPDATES:-30}"
require_channel "$channel"

if [[ ! "$accepted_updates" =~ ^[1-9][0-9]*$ ]] || (( accepted_updates < 10 )); then
    echo "DHD_HOMOGRAPHY_ACCEPTED_UPDATES must be an integer of at least 10." >&2
    exit 2
fi

ensure_calibration_tools
executable="$CALIBRATION_BUILD_DIR/fixed_homography_calibrator"
aruco_config="$OPENCV_DIR/aruco_board_config.txt"
camera_calibration="$OPENCV_DIR/camera_calibration_ch${channel}.yml"
output="$OPENCV_DIR/homography_ch${channel}.yml"
source_url="$(resolve_calibration_source "$channel" "$source_override")"

for required in "$executable" "$aruco_config" "$camera_calibration"; do
    if [[ ! -f "$required" ]]; then
        echo "Required file not found: $required" >&2
        exit 2
    fi
done
if ! awk -v channel="$channel" '$1 == "BOARD" && $2 == channel { found=1 } END { exit !found }' "$aruco_config"; then
    echo "Channel $channel BOARD is not configured. Run ConfigureArucoChannel.sh first." >&2
    exit 2
fi
marker_count="$(awk -v channel="$channel" '$1 == "MARKER" && $2 == channel { count++ } END { print count+0 }' "$aruco_config")"
if (( marker_count < 4 )); then
    echo "Channel $channel needs at least four configured markers." >&2
    exit 2
fi

echo "Channel $channel fixed Homography calibration"
echo "Input (must match the production server stream): $source_url"
echo "Keep the fire/smoke server stopped and all configured floor markers visible."

"$executable" "$source_url" --channel "$channel" \
    --aruco-config "$aruco_config" \
    --camera-calibration "$camera_calibration" \
    --output "$output" \
    --accepted-updates "$accepted_updates" --max-frames 900

echo "Saved: $output"
echo "The server integration must load this file for channel $channel at startup."
