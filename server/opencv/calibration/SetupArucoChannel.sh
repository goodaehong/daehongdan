#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
channel="${1:-}"
source_override="${2:-}"

if [[ ! "$channel" =~ ^[1-4]$ ]]; then
    echo "Usage: $0 <channel 1..4> [exact-production-rtsp-source]" >&2
    exit 2
fi

echo "Channel $channel one-step ArUco setup"
echo "Step 1/2: enter factory and marker coordinates."
"$script_dir/ConfigureArucoChannel.sh" "$channel"

echo
echo "Step 2/2: detect installed markers and save a fixed Homography."
"$script_dir/RunFixedHomographyCalibration.sh" "$channel" "$source_override"

echo
echo "Channel $channel setup complete."
echo "The production server must load the saved calibration and Homography files at startup."
