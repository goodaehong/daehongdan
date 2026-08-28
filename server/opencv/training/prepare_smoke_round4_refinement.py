from __future__ import annotations

import argparse
import shutil
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build balanced field-only Round 4 refinement data")
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--positive-repeats", type=int, default=3)
    return parser.parse_args()


def copy_pair(source: Path, output: Path, source_split: str, target_split: str,
              image: Path, suffix: str = "") -> None:
    source_label = source / "labels" / source_split / f"{image.stem}.txt"
    target_stem = f"{image.stem}{suffix}"
    target_images = output / "images" / target_split
    target_labels = output / "labels" / target_split
    target_images.mkdir(parents=True, exist_ok=True)
    target_labels.mkdir(parents=True, exist_ok=True)
    shutil.copy2(image, target_images / f"{target_stem}.jpg")
    shutil.copy2(source_label, target_labels / f"{target_stem}.txt")


def main() -> int:
    args = parse_args()
    if args.output.exists():
        raise SystemExit(f"Output already exists: {args.output}")

    train_images = sorted((args.source / "images" / "train").glob("round4_*.jpg"))
    test_images = sorted((args.source / "images" / "test").glob("round4_*.jpg"))
    positives = 0
    negatives = 0
    for image in train_images:
        label = (args.source / "labels" / "train" / f"{image.stem}.txt").read_text(encoding="utf-8")
        repeats = args.positive_repeats if label.strip() else 1
        positives += bool(label.strip()) * repeats
        negatives += not bool(label.strip())
        for repeat in range(repeats):
            suffix = "" if repeat == 0 else f"_repeat{repeat}"
            copy_pair(args.source, args.output, "train", "train", image, suffix)

    for image in test_images:
        copy_pair(args.source, args.output, "test", "val", image)
        copy_pair(args.source, args.output, "test", "test", image)

    (args.output / "data.yaml").write_text(
        "path: /content/smoke_round4b_bundle/dataset\n"
        "train: images/train\n"
        "val: images/val\n"
        "test: images/test\n\n"
        "names:\n  0: smoke\n  1: fire\n",
        encoding="utf-8",
    )
    print(f"train smoke positives={positives}, hard negatives={negatives}")
    print(f"held-out validation/test images={len(test_images)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
