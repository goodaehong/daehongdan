from __future__ import annotations

import argparse
import csv
import shutil
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np


CAMERA_CROP = (462, 185, 700, 383)  # x, y, width, height in the 1918x1026 recording


@dataclass(frozen=True)
class VideoSpec:
    name: str
    positive_start: float
    positive_end: float
    anchors: dict[float, tuple[int, int, int, int]]


VIDEO_SPECS = (
    VideoSpec(
        "smoke",
        37.5,
        43.5,
        {
            37.5: (345, 55, 430, 150),
            38.0: (340, 50, 435, 150),
            39.0: (330, 30, 450, 155),
            40.0: (325, 25, 455, 175),
            41.0: (330, 45, 500, 175),
            42.0: (325, 30, 510, 175),
            43.0: (340, 25, 470, 150),
            43.5: (350, 30, 455, 140),
        },
    ),
    VideoSpec(
        "smoke2",
        17.5,
        21.5,
        {
            17.5: (350, 80, 420, 150),
            18.0: (340, 65, 430, 155),
            19.0: (320, 35, 480, 180),
            20.0: (335, 60, 490, 190),
            21.0: (350, 70, 470, 170),
            21.5: (360, 80, 450, 155),
        },
    ),
    VideoSpec(
        "smoke3",
        26.5,
        30.5,
        {
            26.5: (350, 60, 430, 150),
            27.0: (335, 25, 460, 170),
            28.0: (320, 10, 510, 185),
            29.0: (335, 10, 480, 165),
            30.0: (330, 15, 490, 170),
            30.5: (345, 25, 465, 155),
        },
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the reviewed round-3 smoke dataset.")
    parser.add_argument("--video", action="append", required=True, type=Path,
                        help="Video path, in the order: smoke, smoke2, smoke3")
    parser.add_argument("--base-dataset", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def read_frame(capture: cv2.VideoCapture, seconds: float) -> np.ndarray:
    capture.set(cv2.CAP_PROP_POS_MSEC, seconds * 1000.0)
    ok, frame = capture.read()
    if not ok or frame is None:
        raise RuntimeError(f"Could not read frame at {seconds:.3f}s")
    x, y, width, height = CAMERA_CROP
    crop = frame[y:y + height, x:x + width]
    if crop.shape[:2] != (height, width):
        raise RuntimeError(f"Unexpected video dimensions: {frame.shape}")
    return crop


def interpolate_box(spec: VideoSpec, seconds: float) -> tuple[int, int, int, int]:
    times = sorted(spec.anchors)
    if seconds <= times[0]:
        return spec.anchors[times[0]]
    if seconds >= times[-1]:
        return spec.anchors[times[-1]]
    for left, right in zip(times, times[1:]):
        if left <= seconds <= right:
            alpha = (seconds - left) / (right - left)
            start = np.asarray(spec.anchors[left], dtype=np.float32)
            end = np.asarray(spec.anchors[right], dtype=np.float32)
            return tuple(np.rint(start * (1.0 - alpha) + end * alpha).astype(int))
    raise AssertionError("unreachable")


def yolo_label(box: tuple[int, int, int, int], width: int, height: int) -> str:
    x1, y1, x2, y2 = box
    cx = (x1 + x2) / 2.0 / width
    cy = (y1 + y2) / 2.0 / height
    bw = (x2 - x1) / width
    bh = (y2 - y1) / height
    return f"0 {cx:.6f} {cy:.6f} {bw:.6f} {bh:.6f}\n"


def save_sample(output: Path, split: str, name: str, image: np.ndarray, label: str) -> None:
    image_path = output / "images" / split / f"{name}.jpg"
    label_path = output / "labels" / split / f"{name}.txt"
    image_path.parent.mkdir(parents=True, exist_ok=True)
    label_path.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(image_path), image, [cv2.IMWRITE_JPEG_QUALITY, 94]):
        raise RuntimeError(f"Could not write {image_path}")
    label_path.write_text(label, encoding="utf-8")


def positive_variants(image: np.ndarray):
    yield "orig", image
    yield "bright", cv2.convertScaleAbs(image, alpha=1.08, beta=8)
    yield "dark", cv2.convertScaleAbs(image, alpha=0.92, beta=-6)
    yield "soft", cv2.GaussianBlur(image, (3, 3), 0.65)


def add_video(spec: VideoSpec, video: Path, output: Path, manifest: list[dict[str, str]]) -> None:
    capture = cv2.VideoCapture(str(video))
    if not capture.isOpened():
        raise RuntimeError(f"Could not open {video}")
    duration = capture.get(cv2.CAP_PROP_FRAME_COUNT) / capture.get(cv2.CAP_PROP_FPS)

    positive_times = np.arange(spec.positive_start, spec.positive_end + 0.001, 0.25)
    for index, seconds in enumerate(positive_times):
        frame = read_frame(capture, float(seconds))
        box = interpolate_box(spec, float(seconds))
        label = yolo_label(box, frame.shape[1], frame.shape[0])
        split = "val" if index % 5 == 4 else "train"
        variants = (("orig", frame),) if split == "val" else positive_variants(frame)
        for suffix, variant in variants:
            name = f"round3_{spec.name}_positive_{index:03d}_{suffix}"
            save_sample(output, split, name, variant, label)
            manifest.append({"split": split, "image": f"{name}.jpg", "video": video.name,
                             "seconds": f"{seconds:.2f}", "kind": "smoke_positive", "box": str(box)})

    negative_times = [
        float(seconds) for seconds in np.arange(0.0, max(0.0, duration - 0.25), 1.0)
        if seconds < spec.positive_start - 2.0 or seconds > spec.positive_end + 3.0
    ]
    for index, seconds in enumerate(negative_times):
        frame = read_frame(capture, seconds)
        split = "val" if index % 7 == 6 else "train"
        name = f"round3_{spec.name}_negative_{index:03d}"
        save_sample(output, split, name, frame, "")
        manifest.append({"split": split, "image": f"{name}.jpg", "video": video.name,
                         "seconds": f"{seconds:.2f}", "kind": "hard_negative", "box": ""})
    capture.release()


def main() -> int:
    args = parse_args()
    if len(args.video) != len(VIDEO_SPECS):
        raise SystemExit("Exactly three --video arguments are required: smoke, smoke2, smoke3")
    if args.output.exists():
        raise SystemExit(f"Output already exists: {args.output}")

    shutil.copytree(args.base_dataset, args.output)
    manifest: list[dict[str, str]] = []
    for spec, video in zip(VIDEO_SPECS, args.video):
        add_video(spec, video, args.output, manifest)

    yaml_path = args.output / "data.yaml"
    yaml_path.write_text(
        f"path: {args.output.as_posix()}\n"
        "train: images/train\n"
        "val: images/val\n\n"
        "names:\n  0: smoke\n  1: fire\n",
        encoding="utf-8",
    )
    with (args.output.parent / "round3_manifest.csv").open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=["split", "image", "video", "seconds", "kind", "box"])
        writer.writeheader()
        writer.writerows(manifest)

    positives = sum(row["kind"] == "smoke_positive" for row in manifest)
    negatives = sum(row["kind"] == "hard_negative" for row in manifest)
    print(f"Added positives={positives}, negatives={negatives}")
    print(f"Dataset: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
