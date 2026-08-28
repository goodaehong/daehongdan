from __future__ import annotations

import argparse
import csv
import shutil
from dataclasses import dataclass
from pathlib import Path

import cv2
import numpy as np


CAMERA_CROP = (462, 185, 700, 383)  # x, y, width, height in the screen recordings


@dataclass(frozen=True)
class VideoSpec:
    name: str
    split: str
    positive_start: float
    positive_end: float
    anchors: dict[float, tuple[int, int, int, int]]
    negative_ranges: tuple[tuple[float, float], ...]


# Boxes cover only the visible smoke plume. They deliberately stop above the
# white platform, yellow emitter and ArUco marker that polluted Round 3 labels.
VIDEO_SPECS = (
    VideoSpec(
        "smoke",
        "train",
        37.75,
        43.25,
        {
            37.75: (345, 48, 397, 104),
            38.00: (338, 38, 405, 105),
            39.00: (325, 16, 418, 108),
            40.00: (315, 8, 420, 106),
            41.00: (312, 18, 432, 112),
            42.00: (318, 10, 438, 108),
            43.00: (330, 8, 438, 101),
            43.25: (338, 13, 430, 98),
        },
        ((0.0, 35.0), (46.0, 47.0)),
    ),
    VideoSpec(
        "smoke2",
        "train",
        17.75,
        21.25,
        {
            17.75: (345, 55, 400, 108),
            18.00: (337, 39, 410, 108),
            19.00: (318, 16, 438, 112),
            20.00: (323, 28, 447, 118),
            21.00: (338, 43, 438, 112),
            21.25: (346, 51, 426, 108),
        },
        ((0.0, 15.0), (24.0, 28.0)),
    ),
    # Kept completely out of training as a field-scene generalization test.
    VideoSpec(
        "smoke3",
        "test",
        26.75,
        30.25,
        {
            26.75: (340, 28, 405, 105),
            27.00: (328, 12, 420, 108),
            28.00: (305, 0, 455, 116),
            29.00: (316, 0, 445, 104),
            30.00: (315, 0, 450, 108),
            30.25: (326, 5, 440, 105),
        },
        ((0.0, 24.0), (33.0, 36.0)),
    ),
    VideoSpec(
        "new_20260827",
        "train",
        # Smoke begins near 28 s, but it is not visually boxable until ~30.5 s.
        # The ambiguous 28.0-30.0 s transition is excluded, not mislabeled.
        30.5,
        39.5,
        {
            30.5: (385, 47, 435, 106),
            31.0: (375, 29, 445, 108),
            32.0: (358, 8, 460, 108),
            33.0: (348, 7, 455, 105),
            34.0: (342, 5, 447, 103),
            35.0: (350, 4, 440, 102),
            36.0: (360, 6, 435, 102),
            37.0: (342, 4, 432, 106),
            38.0: (325, 2, 430, 110),
            39.0: (318, 2, 438, 112),
            39.5: (315, 2, 442, 113),
        },
        # 10-14 s contains the old detector's orange overlay and is excluded.
        ((0.0, 9.5), (15.0, 27.5)),
    ),
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the corrected Round 4 smoke dataset.")
    parser.add_argument("--video", action="append", required=True, type=Path,
                        help="Videos in order: smoke, smoke2, smoke3, new_20260827")
    parser.add_argument("--base-dataset", required=True, type=Path,
                        help="Round 3 combined dataset; Round 3 samples and .npy caches are excluded")
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
            return tuple(int(value) for value in np.rint(start * (1.0 - alpha) + end * alpha).astype(int))
    raise AssertionError("unreachable")


def yolo_label(box: tuple[int, int, int, int], width: int, height: int) -> str:
    x1, y1, x2, y2 = box
    return (
        f"0 {(x1 + x2) / 2 / width:.6f} {(y1 + y2) / 2 / height:.6f} "
        f"{(x2 - x1) / width:.6f} {(y2 - y1) / height:.6f}\n"
    )


def save_sample(output: Path, split: str, name: str, image: np.ndarray, label: str) -> None:
    image_path = output / "images" / split / f"{name}.jpg"
    label_path = output / "labels" / split / f"{name}.txt"
    image_path.parent.mkdir(parents=True, exist_ok=True)
    label_path.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(image_path), image, [cv2.IMWRITE_JPEG_QUALITY, 94]):
        raise RuntimeError(f"Could not write {image_path}")
    label_path.write_text(label, encoding="utf-8")


def copy_clean_base(base: Path, output: Path) -> int:
    copied = 0
    for split in ("train", "val"):
        source_images = base / "images" / split
        source_labels = base / "labels" / split
        target_images = output / "images" / split
        target_labels = output / "labels" / split
        target_images.mkdir(parents=True, exist_ok=True)
        target_labels.mkdir(parents=True, exist_ok=True)
        for image in source_images.glob("*.jpg"):
            if image.name.startswith("round3_"):
                continue
            label = source_labels / f"{image.stem}.txt"
            if not label.exists():
                raise RuntimeError(f"Missing label for {image}")
            shutil.copy2(image, target_images / image.name)
            shutil.copy2(label, target_labels / label.name)
            copied += 1
    return copied


def add_video(spec: VideoSpec, video: Path, output: Path, manifest: list[dict[str, str]]) -> None:
    capture = cv2.VideoCapture(str(video))
    if not capture.isOpened():
        raise RuntimeError(f"Could not open {video}")

    for index, seconds in enumerate(np.arange(spec.positive_start, spec.positive_end + 0.001, 0.5)):
        seconds = float(seconds)
        frame = read_frame(capture, seconds)
        box = interpolate_box(spec, seconds)
        name = f"round4_{spec.name}_positive_{index:03d}"
        save_sample(output, spec.split, name, frame, yolo_label(box, frame.shape[1], frame.shape[0]))
        manifest.append({"split": spec.split, "image": f"{name}.jpg", "video": video.name,
                         "seconds": f"{seconds:.2f}", "kind": "smoke_positive", "box": str(box)})

    negative_index = 0
    for start, end in spec.negative_ranges:
        for seconds in np.arange(start, end + 0.001, 1.0):
            seconds = float(seconds)
            frame = read_frame(capture, seconds)
            name = f"round4_{spec.name}_negative_{negative_index:03d}"
            save_sample(output, spec.split, name, frame, "")
            manifest.append({"split": spec.split, "image": f"{name}.jpg", "video": video.name,
                             "seconds": f"{seconds:.2f}", "kind": "hard_negative", "box": ""})
            negative_index += 1
    capture.release()


def save_qa(output: Path, manifest: list[dict[str, str]]) -> None:
    positives = [row for row in manifest if row["kind"] == "smoke_positive"]
    selected = positives[::max(1, len(positives) // 20)][:20]
    tiles: list[np.ndarray] = []
    for row in selected:
        image = cv2.imread(str(output / "images" / row["split"] / row["image"]))
        box_text = row["box"].replace("np.int64(", "").replace("(", "").replace(")", "")
        box = tuple(int(value) for value in box_text.split(","))
        cv2.rectangle(image, (box[0], box[1]), (box[2], box[3]), (0, 255, 0), 2)
        cv2.putText(image, f"{row['video']} {row['seconds']}s", (8, 24),
                    cv2.FONT_HERSHEY_SIMPLEX, .6, (0, 255, 255), 2)
        tiles.append(cv2.resize(image, (420, 230)))
    rows = [np.hstack(tiles[index:index + 4]) for index in range(0, len(tiles), 4)]
    cv2.imwrite(str(output.parent / "round4_qa.jpg"), np.vstack(rows), [cv2.IMWRITE_JPEG_QUALITY, 95])


def main() -> int:
    args = parse_args()
    if len(args.video) != len(VIDEO_SPECS):
        raise SystemExit("Exactly four --video arguments are required")
    if args.output.exists():
        raise SystemExit(f"Output already exists: {args.output}")

    base_count = copy_clean_base(args.base_dataset, args.output)
    manifest: list[dict[str, str]] = []
    for spec, video in zip(VIDEO_SPECS, args.video):
        add_video(spec, video, args.output, manifest)

    (args.output / "data.yaml").write_text(
        "path: /content/smoke_round4_bundle/dataset\n"
        "train: images/train\n"
        "val: images/val\n"
        "test: images/test\n\n"
        "names:\n  0: smoke\n  1: fire\n",
        encoding="utf-8",
    )
    manifest_path = args.output.parent / "round4_manifest.csv"
    with manifest_path.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=["split", "image", "video", "seconds", "kind", "box"])
        writer.writeheader()
        writer.writerows(manifest)
    save_qa(args.output, manifest)

    positives = sum(row["kind"] == "smoke_positive" for row in manifest)
    negatives = sum(row["kind"] == "hard_negative" for row in manifest)
    print(f"Copied clean base images={base_count}")
    print(f"Added field positives={positives}, hard negatives={negatives}")
    print(f"Dataset: {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
