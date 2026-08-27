from __future__ import annotations

import argparse
from pathlib import Path

from ultralytics import YOLO


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Smoke NCNN frame-level smoke check")
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--images", required=True, type=Path)
    parser.add_argument("--confidence", type=float, default=0.15)
    return parser.parse_args()


def has_smoke(model: YOLO, image: Path, confidence: float) -> bool:
    result = model.predict(
        str(image), imgsz=(256, 416), conf=confidence, verbose=False
    )[0]
    return any(
        int(class_id) == 0 and float(score) >= confidence
        for class_id, score in zip(result.boxes.cls, result.boxes.conf)
    )


def main() -> int:
    args = parse_args()
    positive = sorted(args.images.glob("*positive*.jpg"))
    negative = sorted(args.images.glob("*negative*.jpg"))
    if not positive or not negative:
        raise SystemExit("Both positive and negative image names are required")

    model = YOLO(str(args.model), task="detect")
    positive_hits = sum(has_smoke(model, image, args.confidence) for image in positive)
    negative_hits = sum(has_smoke(model, image, args.confidence) for image in negative)

    print(f"positive_frames={len(positive)}")
    print(f"positive_hits={positive_hits}")
    print(f"frame_recall={positive_hits / len(positive):.4f}")
    print(f"negative_frames={len(negative)}")
    print(f"negative_false_hits={negative_hits}")
    print(f"negative_false_hit_rate={negative_hits / len(negative):.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
