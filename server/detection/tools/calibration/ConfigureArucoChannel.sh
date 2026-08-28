#!/usr/bin/env bash

set -euo pipefail
export LC_ALL=C

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
opencv_dir="$(cd -- "$script_dir/.." && pwd)"
channel="${1:-}"
config_path="${2:-$opencv_dir/aruco_board_config.txt}"

if [[ ! "$channel" =~ ^[1-4]$ ]]; then
    echo "Usage: $0 <channel 1..4> [config-path]" >&2
    exit 2
fi

is_number() {
    [[ "$1" =~ ^[-+]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][-+]?[0-9]+)?$ ]]
}

read_number() {
    local prompt="$1" default_value="$2" value
    while true; do
        read -r -p "$prompt [$default_value]: " value
        value="${value:-$default_value}"
        if is_number "$value"; then printf '%s\n' "$value"; return; fi
        echo "Enter a number using a decimal point, for example 31.5." >&2
    done
}

read_required_number() {
    local prompt="$1" value
    while true; do
        read -r -p "$prompt (required): " value
        if is_number "$value"; then printf '%s\n' "$value"; return; fi
        echo "Enter a number using a decimal point, for example 31.5." >&2
    done
}

less_than() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a < b) }'; }
less_or_equal() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a <= b) }'; }
format_number() { awk -v value="$1" 'BEGIN { printf "%.8g", value }'; }

factory_defaults=(0 0 60 60)
scale_default=50
board_values=()
if [[ -f "$config_path" ]]; then
    mapfile -t existing_factory < <(awk '$1 == "FACTORY" { print $2; print $3; print $4; print $5; exit }' "$config_path")
    if (( ${#existing_factory[@]} == 4 )); then factory_defaults=("${existing_factory[@]}"); fi
    existing_scale="$(awk '$1 == "MODEL_SCALE" { print $2; exit }' "$config_path")"
    if [[ -n "$existing_scale" ]]; then scale_default="$existing_scale"; fi
    mapfile -t board_values < <(awk -v channel="$channel" '$1 == "BOARD" && $2 == channel { print $3; print $4; print $5; print $6; exit }' "$config_path")
fi

echo "Configure channel $channel ArUco factory coordinates"
echo "Coordinates are real factory metres, not camera pixels or model centimetres."
echo "Press Enter to keep a displayed default."

factory_min_x="$(read_number "Factory minimum X (m)" "${factory_defaults[0]}")"
factory_min_y="$(read_number "Factory minimum Y (m)" "${factory_defaults[1]}")"
factory_max_x="$(read_number "Factory maximum X (m)" "${factory_defaults[2]}")"
factory_max_y="$(read_number "Factory maximum Y (m)" "${factory_defaults[3]}")"
model_scale="$(read_number "Model scale (1 model metre represents this many factory metres)" "$scale_default")"

if (( ${#board_values[@]} == 4 )); then
    board_min_x="$(read_number "Channel $channel area minimum X (factory m)" "${board_values[0]}")"
    board_min_y="$(read_number "Channel $channel area minimum Y (factory m)" "${board_values[1]}")"
    board_max_x="$(read_number "Channel $channel area maximum X (factory m)" "${board_values[2]}")"
    board_max_y="$(read_number "Channel $channel area maximum Y (factory m)" "${board_values[3]}")"
else
    echo "Channel $channel has no saved coordinates. Enter all four channel bounds."
    board_min_x="$(read_required_number "Channel $channel area minimum X (factory m)")"
    board_min_y="$(read_required_number "Channel $channel area minimum Y (factory m)")"
    board_max_x="$(read_required_number "Channel $channel area maximum X (factory m)")"
    board_max_y="$(read_required_number "Channel $channel area maximum Y (factory m)")"
fi

if ! less_than "$factory_min_x" "$factory_max_x" || \
   ! less_than "$factory_min_y" "$factory_max_y" || \
   ! less_than "$board_min_x" "$board_max_x" || \
   ! less_than "$board_min_y" "$board_max_y" || \
   less_than "$board_min_x" "$factory_min_x" || \
   less_than "$board_min_y" "$factory_min_y" || \
   less_than "$factory_max_x" "$board_max_x" || \
   less_than "$factory_max_y" "$board_max_y" || \
   less_or_equal "$model_scale" 0; then
    echo "Factory/channel bounds or model scale are invalid." >&2
    exit 2
fi

while true; do
    read -r -p "Visible marker IDs, comma separated [0,1,2,3,4]: " raw_ids
    raw_ids="${raw_ids:-0,1,2,3,4}"
    IFS=',' read -r -a marker_ids <<< "$raw_ids"
    declare -A seen_ids=()
    valid_ids=true
    for index in "${!marker_ids[@]}"; do
        marker_ids[$index]="${marker_ids[$index]//[[:space:]]/}"
        id="${marker_ids[$index]}"
        if [[ ! "$id" =~ ^[0-9]+$ ]] || (( id < 0 || id > 49 )) || [[ -n "${seen_ids[$id]:-}" ]]; then
            valid_ids=false
            break
        fi
        seen_ids[$id]=1
    done
    if [[ "$valid_ids" == true ]] && (( ${#marker_ids[@]} >= 4 )); then break; fi
    echo "Enter at least four unique DICT_4X4_50 IDs, for example 0,1,2,3,4." >&2
    unset seen_ids
done

marker_records=()
for id in "${marker_ids[@]}"; do
    existing_marker=()
    if [[ -f "$config_path" ]]; then
        mapfile -t existing_marker < <(awk -v channel="$channel" -v id="$id" '$1 == "MARKER" && $2 == channel && $3 == id { print $4; print $5; print $6; print $7; exit }' "$config_path")
    fi
    center_x="$(awk -v a="$board_min_x" -v b="$board_max_x" 'BEGIN { printf "%.8g", (a+b)/2 }')"
    center_y="$(awk -v a="$board_min_y" -v b="$board_max_y" 'BEGIN { printf "%.8g", (a+b)/2 }')"
    side_cm=4
    rotation=0
    if (( ${#existing_marker[@]} == 4 )); then
        center_x="${existing_marker[0]}"
        center_y="${existing_marker[1]}"
        side_cm="$(awk -v side="${existing_marker[2]}" 'BEGIN { printf "%.8g", side*100 }')"
        rotation="${existing_marker[3]}"
    fi
    echo "Marker ID $id"
    if (( ${#existing_marker[@]} == 4 )); then
        x="$(read_number "  centre factory X (m)" "$center_x")"
        y="$(read_number "  centre factory Y (m)" "$center_y")"
    else
        x="$(read_required_number "  centre factory X (m)")"
        y="$(read_required_number "  centre factory Y (m)")"
    fi
    side_cm="$(read_number "  printed BLACK square side (model cm)" "$side_cm")"
    rotation="$(read_number "  clockwise rotation from printed upright direction (deg)" "$rotation")"
    if less_than "$x" "$board_min_x" || less_than "$board_max_x" "$x" || \
       less_than "$y" "$board_min_y" || less_than "$board_max_y" "$y" || \
       less_or_equal "$side_cm" 0; then
        echo "Marker ID $id lies outside the channel area or has invalid size." >&2
        exit 2
    fi
    side_m="$(awk -v side="$side_cm" 'BEGIN { printf "%.8g", side/100 }')"
    marker_records+=("MARKER $channel $id $(format_number "$x") $(format_number "$y") $side_m $(format_number "$rotation")")
done

mkdir -p -- "$(dirname -- "$config_path")"
temporary="$(mktemp "${config_path}.tmp.XXXXXX")"
other_geometry="$(mktemp "${config_path}.geometry.XXXXXX")"
cleanup() { rm -f -- "$temporary" "$other_geometry"; }
trap cleanup EXIT
if [[ -f "$config_path" ]]; then
    awk -v channel="$channel" '($1 == "BOARD" || $1 == "MARKER") && $2 != channel { print }' "$config_path" > "$other_geometry"
fi

{
    echo "# Fixed ArUco installation geometry. Coordinates are real factory metres."
    echo "VERSION 1"
    echo "DICTIONARY DICT_4X4_50"
    echo "GRID 60 0 59"
    echo "FACTORY $(format_number "$factory_min_x") $(format_number "$factory_min_y") $(format_number "$factory_max_x") $(format_number "$factory_max_y")"
    echo "MODEL_SCALE $(format_number "$model_scale")"
    echo "# minimum markers, minimum inlier corners, maximum RMS px, hold ms, update frames, smoothing"
    echo "QUALITY 4 12 2.0 1500 1 0.45"
    echo
    cat "$other_geometry"
    echo
    echo "# Channel $channel"
    echo "BOARD $channel $(format_number "$board_min_x") $(format_number "$board_min_y") $(format_number "$board_max_x") $(format_number "$board_max_y")"
    printf '%s\n' "${marker_records[@]}"
} > "$temporary"

timestamp="$(date +%Y%m%d-%H%M%S)"
if [[ -f "$config_path" ]]; then
    backup="${config_path}.${timestamp}.bak"
    cp -- "$config_path" "$backup"
    echo "Backup: $backup"
fi
mv -- "$temporary" "$config_path"
echo "Saved: $config_path"

homography_path="$opencv_dir/homography_ch${channel}.yml"
if [[ -f "$homography_path" ]]; then
    stale_backup="${homography_path}.${timestamp}.stale.bak"
    mv -- "$homography_path" "$stale_backup"
    echo "Old Homography disabled: $stale_backup"
fi
echo "Next: $script_dir/RunFixedHomographyCalibration.sh $channel"
echo "Or next time run $script_dir/SetupArucoChannel.sh $channel for both steps."
