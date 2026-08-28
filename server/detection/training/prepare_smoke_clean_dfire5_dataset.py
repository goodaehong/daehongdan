from __future__ import annotations

import argparse
import csv
import shutil
from pathlib import Path

import cv2
import numpy as np

import build_smoke_label_review as review


COLAB_ROOT = Path("/content/smoke_clean_dfire5_bundle/dataset")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build clean D-Fire + five reviewed field-video datasets"
    )
    parser.add_argument("--dfire-dataset", required=True, type=Path)
    parser.add_argument("--video", action="append", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args()


def sanitize_label(text: str) -> str | None:
    sanitized: list[str] = []
    for line in text.splitlines():
        if not line.strip():
            continue
        fields = line.split()
        if len(fields) != 5:
            return None
        try:
            class_id = int(fields[0])
            cx, cy, width, height = (float(value) for value in fields[1:])
        except ValueError:
            return None
        if class_id not in (0, 1):
            return None
        if width <= 0.0 or height <= 0.0:
            return None

        x1 = max(0.0, cx - width / 2.0)
        y1 = max(0.0, cy - height / 2.0)
        x2 = min(1.0, cx + width / 2.0)
        y2 = min(1.0, cy + height / 2.0)
        clipped_width = x2 - x1
        clipped_height = y2 - y1
        if clipped_width <= 1e-9 or clipped_height <= 1e-9:
            return None
        clipped_cx = (x1 + x2) / 2.0
        clipped_cy = (y1 + y2) / 2.0
        sanitized.append(
            f"{class_id} {clipped_cx:.6f} {clipped_cy:.6f} "
            f"{clipped_width:.6f} {clipped_height:.6f}"
        )
    return "\n".join(sanitized) + ("\n" if sanitized else "")


def copy_clean_dfire(source: Path, output: Path) -> tuple[list[str], list[str], list[str]]:
    train_images: list[str] = []
    val_images: list[str] = []
    excluded: list[str] = []
    for split, target_list in (("train", train_images), ("val", val_images)):
        source_images = source / "images" / split
        source_labels = source / "labels" / split
        target_images = output / "images" / split
        target_labels = output / "labels" / split
        target_images.mkdir(parents=True, exist_ok=True)
        target_labels.mkdir(parents=True, exist_ok=True)
        for image in sorted(source_images.glob("dfire_*.jpg")):
            label = source_labels / f"{image.stem}.txt"
            if not label.exists():
                excluded.append(f"{split}/{image.name}: missing label")
                continue
            text = label.read_text(encoding="utf-8")
            sanitized = sanitize_label(text)
            if sanitized is None:
                excluded.append(f"{split}/{image.name}: malformed label")
                continue
            shutil.copy2(image, target_images / image.name)
            (target_labels / label.name).write_text(sanitized, encoding="utf-8")
            target_list.append(f"images/{split}/{image.name}")
    return train_images, val_images, excluded


def read_crop(capture: cv2.VideoCapture, seconds: float) -> np.ndarray:
    capture.set(cv2.CAP_PROP_POS_MSEC, seconds * 1000.0)
    ok, frame = capture.read()
    if not ok or frame is None:
        raise RuntimeError(f"Could not read frame at {seconds:.2f}s")
    x, y, width, height = review.CAMERA_CROP
    crop = frame[y:y + height, x:x + width]
    if crop.shape[:2] != (height, width):
        raise RuntimeError(f"Unexpected crop shape {crop.shape} at {seconds:.2f}s")
    return crop


def save_field_sample(output: Path, split: str, name: str, image: np.ndarray,
                      label: str) -> str:
    image_path = output / "images" / split / f"{name}.jpg"
    label_path = output / "labels" / split / f"{name}.txt"
    image_path.parent.mkdir(parents=True, exist_ok=True)
    label_path.parent.mkdir(parents=True, exist_ok=True)
    if not cv2.imwrite(str(image_path), image, [cv2.IMWRITE_JPEG_QUALITY, 95]):
        raise RuntimeError(f"Could not write {image_path}")
    label_path.write_text(label, encoding="utf-8")
    return f"images/{split}/{image_path.name}"


def add_field_video(module, spec, video: Path, output: Path,
                    manifest: list[dict[str, str]]) -> list[str]:
    capture = cv2.VideoCapture(str(video))
    if not capture.isOpened():
        raise RuntimeError(f"Could not open {video}")
    split = "test" if spec.name == "smoke3" else "train"
    image_paths: list[str] = []

    for index, value in enumerate(
        np.arange(spec.positive_start, spec.positive_end + 0.001, 0.5)
    ):
        seconds = float(value)
        image = read_crop(capture, seconds)
        box = module.interpolate_box(spec, seconds)
        label = module.yolo_label(box, image.shape[1], image.shape[0])
        name = f"clean5_{spec.name}_positive_{index:03d}"
        image_paths.append(save_field_sample(output, split, name, image, label))
        manifest.append({
            "split": split,
            "image": f"{name}.jpg",
            "video": video.name,
            "seconds": f"{seconds:.2f}",
            "kind": "smoke_positive",
            "box": str(box),
        })

    negative_index = 0
    for start, end in spec.negative_ranges:
        for value in np.arange(start, end + 0.001, 1.0):
            seconds = float(value)
            image = read_crop(capture, seconds)
            name = f"clean5_{spec.name}_negative_{negative_index:03d}"
            image_paths.append(save_field_sample(output, split, name, image, ""))
            manifest.append({
                "split": split,
                "image": f"{name}.jpg",
                "video": video.name,
                "seconds": f"{seconds:.2f}",
                "kind": "hard_negative",
                "box": "",
            })
            negative_index += 1
    capture.release()
    return image_paths


def write_list(path: Path, relative_paths: list[str]) -> None:
    path.write_text(
        "".join(f"{(COLAB_ROOT / item).as_posix()}\n" for item in relative_paths),
        encoding="utf-8",
    )


def write_yaml(path: Path, train_list: str) -> None:
    val_list = "stage_a_val.txt" if "stage_a" in train_list else "final_val.txt"
    path.write_text(
        f"path: {COLAB_ROOT.as_posix()}\n"
        f"train: {train_list}\n"
        f"val: {val_list}\n"
        "test: field_holdout.txt\n\n"
        "names:\n  0: smoke\n  1: fire\n",
        encoding="utf-8",
    )


def split_field_paths(paths: list[str]) -> tuple[list[str], list[str]]:
    train: list[str] = []
    val: list[str] = []
    for kind in ("positive", "negative"):
        selected = [path for path in paths if f"_{kind}_" in path]
        for index, path in enumerate(selected):
            (val if index % 5 == 0 else train).append(path)
    return train, val


def oversample_field(paths: list[str]) -> list[str]:
    positives = [path for path in paths if "_positive_" in path]
    negatives = [path for path in paths if "_negative_" in path]
    return positives * 12 + negatives * 3


def main() -> int:
    args = parse_args()
    module = review.load_dataset_module()
    specs = review.make_video_specs(module)
    if len(args.video) != len(specs):
        raise SystemExit(f"Expected {len(specs)} videos in review order")
    if args.output.exists():
        raise SystemExit(f"Output already exists: {args.output}")
    args.output.mkdir(parents=True)

    dfire_train, dfire_val, excluded = copy_clean_dfire(args.dfire_dataset, args.output)
    manifest: list[dict[str, str]] = []
    stage_a_field_train: list[str] = []
    stage_a_field_val: list[str] = []
    holdout_field: list[str] = []
    final_field_train: list[str] = []
    final_field_val: list[str] = []
    for spec, video in zip(specs, args.video):
        paths = add_field_video(module, spec, video, args.output, manifest)
        train_paths, val_paths = split_field_paths(paths)
        final_field_train.extend(train_paths)
        final_field_val.extend(val_paths)
        if spec.name == "smoke3":
            holdout_field.extend(paths)
        else:
            stage_a_field_train.extend(train_paths)
            stage_a_field_val.extend(val_paths)

    write_list(
        args.output / "stage_a_train.txt",
        dfire_train + oversample_field(stage_a_field_train),
    )
    write_list(
        args.output / "stage_a_val.txt",
        dfire_val + (stage_a_field_val + holdout_field) * 4,
    )
    write_list(
        args.output / "final_train.txt",
        dfire_train + oversample_field(final_field_train),
    )
    write_list(
        args.output / "final_val.txt",
        dfire_val + final_field_val * 4,
    )
    write_list(args.output / "dfire_val.txt", dfire_val)
    write_list(args.output / "field_holdout.txt", holdout_field)
    write_yaml(args.output / "data_stage_a.yaml", "stage_a_train.txt")
    write_yaml(args.output / "data_final.yaml", "final_train.txt")

    with (args.output / "field_manifest.csv").open(
        "w", newline="", encoding="utf-8-sig"
    ) as handle:
        writer = csv.DictWriter(
            handle, fieldnames=["split", "image", "video", "seconds", "kind", "box"]
        )
        writer.writeheader()
        writer.writerows(manifest)
    (args.output / "excluded_dfire_labels.txt").write_text(
        "\n".join(excluded) + ("\n" if excluded else ""), encoding="utf-8"
    )

    print(f"dfire_train={len(dfire_train)} dfire_val={len(dfire_val)} excluded={len(excluded)}")
    print(
        f"stage_a_field_train={len(stage_a_field_train)} "
        f"stage_a_field_val={len(stage_a_field_val)} holdout_field={len(holdout_field)}"
    )
    print(
        f"final_field_train={len(final_field_train)} "
        f"final_field_val={len(final_field_val)}"
    )
    print(f"output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
