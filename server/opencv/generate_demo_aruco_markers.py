from pathlib import Path

import cv2
import numpy as np


OUTPUT = Path(__file__).resolve().parent / "generated_demo_aruco_markers"
DICTIONARY = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)
MARKER_IDS = (0, 1, 2, 3)
MARKER_PIXELS = 600
QUIET_ZONE_PIXELS = 100


def generate_marker(marker_id: int) -> np.ndarray:
    if hasattr(cv2.aruco, "generateImageMarker"):
        marker = cv2.aruco.generateImageMarker(
            DICTIONARY, marker_id, MARKER_PIXELS)
    else:
        marker = np.zeros((MARKER_PIXELS, MARKER_PIXELS), dtype=np.uint8)
        cv2.aruco.drawMarker(DICTIONARY, marker_id, MARKER_PIXELS, marker, 1)
    return cv2.copyMakeBorder(
        marker,
        QUIET_ZONE_PIXELS,
        QUIET_ZONE_PIXELS,
        QUIET_ZONE_PIXELS,
        QUIET_ZONE_PIXELS,
        cv2.BORDER_CONSTANT,
        value=255,
    )


OUTPUT.mkdir(parents=True, exist_ok=True)
for marker_id in MARKER_IDS:
    image = generate_marker(marker_id)
    cv2.imwrite(str(OUTPUT / f"aruco_4x4_50_id_{marker_id}.png"), image)
print(f"generated {len(MARKER_IDS)} markers in {OUTPUT}")
