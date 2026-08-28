from __future__ import annotations

import argparse
import csv
from pathlib import Path

import cv2
from ultralytics import YOLO

import build_smoke_label_review as review
import prepare_smoke_round4_dataset as round4


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Audit smoke confidence on five reviewed videos")
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--video", action="append", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--interval", type=float, default=0.5)
    return parser.parse_args()


def frame_kind(spec, seconds: float) -> str:
    if spec.positive_start - 1e-6 <= seconds <= spec.positive_end + 1e-6:
        return "positive"
    if any(start - 1e-6 <= seconds <= end + 1e-6 for start, end in spec.negative_ranges):
        return "negative"
    return "ignored"


def main() -> int:
    args = parse_args()
    specs = review.make_video_specs(round4)
    if len(args.video) != len(specs):
        raise SystemExit(f"Expected {len(specs)} videos, got {len(args.video)}")

    model = YOLO(str(args.model), task="detect")
    rows: list[dict[str, object]] = []
    x, y, width, height = review.CAMERA_CROP

    for spec, video in zip(specs, args.video):
        capture = cv2.VideoCapture(str(video))
        if not capture.isOpened():
            raise RuntimeError(f"Could not open {video}")
        fps = capture.get(cv2.CAP_PROP_FPS) or 30.0
        duration = capture.get(cv2.CAP_PROP_FRAME_COUNT) / fps
        seconds = 0.0
        while seconds <= duration + 1e-6:
            capture.set(cv2.CAP_PROP_POS_MSEC, seconds * 1000.0)
            ok, frame = capture.read()
            if not ok:
                break
            crop = frame[y:y + height, x:x + width]
            result = model.predict(crop, imgsz=(256, 416), conf=0.001, verbose=False)[0]
            scores = [
                float(score)
                for class_id, score in zip(
                    result.boxes.cls.cpu().tolist(), result.boxes.conf.cpu().tolist()
                )
                if int(class_id) == 0
            ]
            rows.append(
                {
                    "video": spec.name,
                    "seconds": round(seconds, 3),
                    "kind": frame_kind(spec, seconds),
                    "smoke_confidence": max(scores, default=0.0),
                }
            )
            seconds += args.interval
        capture.release()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    for threshold in (0.10, 0.20, 0.30, 0.40, 0.50, 0.60):
        print(f"threshold={threshold:.2f}")
        for spec in specs:
            selected = [row for row in rows if row["video"] == spec.name]
            positives = [row for row in selected if row["kind"] == "positive"]
            negatives = [row for row in selected if row["kind"] == "negative"]
            positive_hits = [row for row in positives if row["smoke_confidence"] >= threshold]
            negative_hits = [row for row in negatives if row["smoke_confidence"] >= threshold]
            first_hit = positive_hits[0]["seconds"] if positive_hits else None
            max_positive = max((float(row["smoke_confidence"]) for row in positives), default=0.0)
            max_negative = max((float(row["smoke_confidence"]) for row in negatives), default=0.0)
            print(
                f"  {spec.name}: pos={len(positive_hits)}/{len(positives)} "
                f"neg_fp={len(negative_hits)}/{len(negatives)} first={first_hit} "
                f"max_pos={max_positive:.3f} max_neg={max_negative:.3f}"
            )
    print(f"csv={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
