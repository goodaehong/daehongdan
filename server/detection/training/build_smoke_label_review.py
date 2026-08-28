from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

import cv2
import numpy as np


ROOT = Path(__file__).resolve().parents[3]
DOWNLOADS = Path(r"C:\Users\3-19\Downloads")
VIDEOS = {
    "screen_20260825": DOWNLOADS / "화면 녹화 중 2026-08-25 212831.mp4",
    "smoke": DOWNLOADS / "연기.mp4",
    "smoke2": DOWNLOADS / "연기2.mp4",
    "smoke3": DOWNLOADS / "연기3.mp4",
    "new_20260827": DOWNLOADS / "녹음 2026-08-27 111718.mp4",
}
OUTPUT = ROOT / "smoke_label_review"

# Smoke-detection source panel: upper-left Ch.1 in the screen recording.
CAMERA_CROP = (462, 185, 700, 383)


def load_dataset_module():
    path = Path(__file__).with_name("prepare_smoke_round4_dataset.py")
    spec = importlib.util.spec_from_file_location("round4_dataset", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not import {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def read_frame(capture: cv2.VideoCapture, seconds: float) -> np.ndarray:
    capture.set(cv2.CAP_PROP_POS_MSEC, seconds * 1000.0)
    ok, frame = capture.read()
    if not ok or frame is None:
        raise RuntimeError(f"Could not read {seconds:.2f}s")
    return frame


def label_frame(frame: np.ndarray, video_name: str, seconds: float,
                kind: str, box: tuple[int, int, int, int] | None) -> np.ndarray:
    x, y, width, height = CAMERA_CROP
    annotated = frame.copy()
    cv2.rectangle(annotated, (x, y), (x + width, y + height), (255, 180, 0), 3)
    if box is not None:
        x1, y1, x2, y2 = box
        cv2.rectangle(annotated, (x + x1, y + y1), (x + x2, y + y2), (0, 255, 0), 5)
    color = (0, 255, 0) if kind == "POSITIVE" else (180, 180, 180)
    cv2.rectangle(annotated, (0, 0), (annotated.shape[1], 70), (0, 0, 0), -1)
    cv2.putText(
        annotated,
        f"{video_name}  t={seconds:05.2f}s  PROPOSED {kind}  Ch.1",
        (20, 48),
        cv2.FONT_HERSHEY_SIMPLEX,
        1.15,
        color,
        3,
        cv2.LINE_AA,
    )
    return annotated


def make_sheet(tiles: list[np.ndarray], path: Path) -> None:
    thumb_width, thumb_height = 640, 342
    thumbs = [cv2.resize(tile, (thumb_width, thumb_height), interpolation=cv2.INTER_AREA) for tile in tiles]
    columns = 3
    blank = np.zeros_like(thumbs[0])
    while len(thumbs) % columns:
        thumbs.append(blank)
    rows = [np.hstack(thumbs[i:i + columns]) for i in range(0, len(thumbs), columns)]
    cv2.imwrite(str(path), np.vstack(rows), [cv2.IMWRITE_JPEG_QUALITY, 94])


def make_video_specs(module):
    # This fifth source is reviewed independently from the old Round 2 data.
    # Boxes intentionally stop above the white platform and ArUco marker.
    screen_spec = module.VideoSpec(
        "screen_20260825",
        "train",
        6.5,
        18.0,
        {
            6.5: (250, 165, 380, 265),
            7.0: (235, 130, 405, 282),
            7.5: (195, 105, 430, 292),
            8.0: (190, 92, 445, 300),
            9.0: (195, 92, 465, 292),
            10.0: (200, 95, 475, 285),
            11.0: (225, 100, 490, 278),
            12.0: (245, 105, 500, 275),
            13.0: (250, 100, 505, 273),
            14.0: (250, 92, 500, 270),
            15.0: (255, 88, 490, 265),
            16.0: (270, 82, 475, 255),
            17.0: (290, 70, 465, 235),
            18.0: (315, 58, 460, 205),
        },
        ((0.0, 6.0), (18.5, 22.0)),
    )
    return (screen_spec, *module.VIDEO_SPECS)


def main() -> int:
    module = load_dataset_module()
    OUTPUT.mkdir(parents=True, exist_ok=True)
    all_review_frames: list[np.ndarray] = []
    video_specs = make_video_specs(module)

    for video_spec in video_specs:
        capture = cv2.VideoCapture(str(VIDEOS[video_spec.name]))
        if not capture.isOpened():
            raise RuntimeError(f"Could not open {VIDEOS[video_spec.name]}")
        tiles: list[np.ndarray] = []

        positive_times = np.arange(
            video_spec.positive_start, video_spec.positive_end + 0.001, 0.5
        )
        for seconds_value in positive_times:
            seconds = float(seconds_value)
            frame = read_frame(capture, seconds)
            box = module.interpolate_box(video_spec, seconds)
            annotated = label_frame(frame, video_spec.name, seconds, "POSITIVE", box)
            annotated_dir = OUTPUT / "annotated_positive" / video_spec.name
            annotated_dir.mkdir(parents=True, exist_ok=True)
            cv2.imwrite(
                str(annotated_dir / f"{seconds:06.2f}s_positive.jpg"),
                annotated,
                [cv2.IMWRITE_JPEG_QUALITY, 95],
            )
            tiles.append(annotated)
            all_review_frames.append(annotated)

        for start, end in video_spec.negative_ranges:
            for seconds_value in np.arange(start, end + 0.001, 2.0):
                seconds = float(seconds_value)
                frame = read_frame(capture, seconds)
                annotated = label_frame(frame, video_spec.name, seconds, "NEGATIVE", None)
                annotated_dir = OUTPUT / "annotated_negative" / video_spec.name
                annotated_dir.mkdir(parents=True, exist_ok=True)
                cv2.imwrite(
                    str(annotated_dir / f"{seconds:06.2f}s_negative.jpg"),
                    annotated,
                    [cv2.IMWRITE_JPEG_QUALITY, 95],
                )
                all_review_frames.append(annotated)

        capture.release()
        make_sheet(tiles, OUTPUT / f"{video_spec.name}_positive_contact_sheet.jpg")

    size = (1280, 684)
    fps = 2.0  # Each proposed labeled sample is held for half a second.
    writer = cv2.VideoWriter(
        str(OUTPUT / "all_labels_review.mp4"),
        cv2.VideoWriter_fourcc(*"mp4v"),
        fps,
        size,
    )
    if not writer.isOpened():
        raise RuntimeError("Could not create review video")
    for frame in all_review_frames:
        writer.write(cv2.resize(frame, size, interpolation=cv2.INTER_AREA))
    writer.release()
    print(f"review_frames={len(all_review_frames)}")
    print(f"output={OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
