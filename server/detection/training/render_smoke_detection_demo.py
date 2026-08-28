from __future__ import annotations

import argparse
from pathlib import Path

import cv2
from ultralytics import YOLO


CH1_CROP = (462, 185, 700, 383)  # x, y, width, height


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Render Ch.1 smoke-model detections")
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--confidence", type=float, default=0.60)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--hold", type=float, default=5.0)
    parser.add_argument("--confirm-hits", type=int, default=1)
    return parser.parse_args()


def draw_text(frame, text: str, x: int, y: int, color: tuple[int, int, int]) -> None:
    scale = max(0.65, frame.shape[1] / 1800.0)
    thickness = max(2, round(scale * 2))
    (width, height), baseline = cv2.getTextSize(
        text, cv2.FONT_HERSHEY_SIMPLEX, scale, thickness
    )
    cv2.rectangle(frame, (x - 6, y - height - 8),
                  (x + width + 6, y + baseline + 5), (0, 0, 0), -1)
    cv2.putText(frame, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX,
                scale, color, thickness, cv2.LINE_AA)


def main() -> int:
    args = parse_args()
    capture = cv2.VideoCapture(str(args.input))
    if not capture.isOpened():
        raise SystemExit(f"Could not open {args.input}")

    fps = capture.get(cv2.CAP_PROP_FPS) or 30.0
    width = int(capture.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(capture.get(cv2.CAP_PROP_FRAME_HEIGHT))
    writer = cv2.VideoWriter(
        str(args.output), cv2.VideoWriter_fourcc(*"mp4v"), fps, (width, height)
    )
    if not writer.isOpened():
        raise SystemExit(f"Could not create {args.output}")

    model = YOLO(str(args.model), task="detect")
    interval_frames = max(1, round(fps * args.interval))
    hold_frames = max(1, round(fps * args.hold))
    x0, y0, crop_width, crop_height = CH1_CROP
    latest_boxes: list[tuple[int, int, int, int, float]] = []
    last_hit = -hold_frames - 1
    consecutive_hits = 0
    hit_times: list[tuple[float, float]] = []
    frame_index = 0

    while True:
        ok, frame = capture.read()
        if not ok:
            break

        if frame_index % interval_frames == 0:
            crop = frame[y0:y0 + crop_height, x0:x0 + crop_width]
            result = model.predict(
                crop, imgsz=(256, 416), conf=args.confidence, verbose=False
            )[0]
            latest_boxes = []
            for xyxy, class_id, score in zip(
                result.boxes.xyxy.cpu().tolist(),
                result.boxes.cls.cpu().tolist(),
                result.boxes.conf.cpu().tolist(),
            ):
                if int(class_id) != 0 or float(score) < args.confidence:
                    continue
                x1, y1, x2, y2 = (round(value) for value in xyxy)
                latest_boxes.append((x0 + x1, y0 + y1, x0 + x2, y0 + y2, float(score)))
            if latest_boxes:
                hit_times.append((frame_index / fps, max(box[4] for box in latest_boxes)))
                consecutive_hits += 1
                if consecutive_hits >= max(1, args.confirm_hits):
                    last_hit = frame_index
            else:
                consecutive_hits = 0

        alert = frame_index - last_hit <= hold_frames
        cv2.rectangle(frame, (x0, y0), (x0 + crop_width, y0 + crop_height), (255, 180, 0), 3)
        for x1, y1, x2, y2, score in latest_boxes:
            cv2.rectangle(frame, (x1, y1), (x2, y2), (0, 80, 255), 4)
            draw_text(frame, f"SMOKE {score:.2f}", x1, max(25, y1 - 8), (0, 180, 255))

        candidate = bool(latest_boxes) and not alert
        state = "ALERT: SMOKE" if alert else (
            f"CANDIDATE {consecutive_hits}/{max(1, args.confirm_hits)}" if candidate else "NORMAL"
        )
        color = (0, 60, 255) if alert else ((0, 190, 255) if candidate else (80, 220, 80))
        draw_text(frame, f"{state} | Ch.1 only | t={frame_index / fps:05.1f}s", 18, 38, color)
        draw_text(frame, "Blue: Ch.1 inference input | Orange: model box", 18, 76, (240, 240, 240))
        writer.write(frame)
        frame_index += 1

    capture.release()
    writer.release()
    print("hits=" + str([(round(t, 1), round(score, 3)) for t, score in hit_times]))
    print(f"output={args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
