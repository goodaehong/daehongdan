from pathlib import Path
import argparse

import cv2
import numpy as np


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--first", type=int, default=0)
    parser.add_argument("--last", type=int, default=24)
    args = parser.parse_args()

    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    dictionary = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)

    for marker_id in range(args.first, args.last + 1):
        if hasattr(cv2.aruco, "generateImageMarker"):
            marker = cv2.aruco.generateImageMarker(dictionary, marker_id, 600)
        else:
            marker = np.zeros((600, 600), dtype=np.uint8)
            cv2.aruco.drawMarker(dictionary, marker_id, 600, marker, 1)
        # 100 px on every side makes the printed image 800 px. When the PDF
        # draws it at 80 mm, the actual black marker is exactly 60 mm.
        printable = cv2.copyMakeBorder(
            marker, 100, 100, 100, 100,
            cv2.BORDER_CONSTANT, value=255)
        path = output / f"aruco_4x4_50_id_{marker_id:02d}.png"
        if not cv2.imwrite(str(path), printable):
            raise RuntimeError(f"failed to write {path}")

    print(f"generated IDs {args.first}..{args.last} in {output}")


if __name__ == "__main__":
    main()
