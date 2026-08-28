from __future__ import annotations

import json
import shutil
from pathlib import Path

import torch
from ultralytics import YOLO


BUNDLE = Path("/content/smoke_round4_bundle")
RUNS = Path("/content/runs")
RUN_NAME = "smoke-round4-corrected-labels-416"


def main() -> int:
    print(f"GPU: {torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CPU'}")
    if not torch.cuda.is_available():
        raise RuntimeError("Colab GPU is not enabled")

    model = YOLO(BUNDLE / "base_round2_best.pt")
    model.train(
        data=BUNDLE / "dataset" / "data.yaml",
        epochs=30,
        patience=8,
        batch=32,
        imgsz=416,
        rect=True,
        device=0,
        workers=2,
        project=RUNS,
        name=RUN_NAME,
        exist_ok=True,
        optimizer="AdamW",
        lr0=0.0003,
        lrf=0.05,
        weight_decay=0.0005,
        freeze=5,
        close_mosaic=5,
        hsv_h=0.008,
        hsv_s=0.15,
        hsv_v=0.12,
        degrees=2.0,
        translate=0.04,
        scale=0.12,
        fliplr=0.5,
        seed=20260827,
        deterministic=True,
        amp=True,
        cache="ram",
        plots=True,
    )

    run_dir = RUNS / RUN_NAME
    best_path = run_dir / "weights" / "best.pt"
    best = YOLO(best_path)
    val_metrics = best.val(data=BUNDLE / "dataset" / "data.yaml", split="val", imgsz=416, device=0)
    test_metrics = best.val(data=BUNDLE / "dataset" / "data.yaml", split="test", imgsz=416, device=0)
    export_path = Path(best.export(format="ncnn", imgsz=(256, 416), device=0))

    results = Path("/content/smoke_round4_results")
    if results.exists():
        shutil.rmtree(results)
    results.mkdir()
    shutil.copy2(best_path, results / "best.pt")
    shutil.copy2(BUNDLE / "round4_manifest.csv", results / "round4_manifest.csv")
    shutil.copy2(BUNDLE / "round4_qa.jpg", results / "round4_qa.jpg")
    shutil.copytree(export_path, results / export_path.name)
    for name in ("args.yaml", "results.csv", "results.png", "confusion_matrix.png",
                 "confusion_matrix_normalized.png"):
        source = run_dir / name
        if source.exists():
            shutil.copy2(source, results / name)

    summary = {
        "gpu": torch.cuda.get_device_name(0),
        "val_map50": float(val_metrics.box.map50),
        "val_map50_95": float(val_metrics.box.map),
        "test_map50": float(test_metrics.box.map50),
        "test_map50_95": float(test_metrics.box.map),
        "export": export_path.name,
    }
    (results / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    shutil.make_archive("/content/smoke_round4_results", "zip", results)
    print(json.dumps(summary, indent=2))
    print("RESULT_ZIP=/content/smoke_round4_results.zip")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
