from __future__ import annotations

import json
import shutil
from pathlib import Path

import torch
from ultralytics import YOLO


BUNDLE = Path("/content/smoke_clean_dfire5_bundle")
DATASET = BUNDLE / "dataset"
RUNS = Path("/content/runs")
RESULTS = Path("/content/smoke_clean_dfire5_results")


def metric_dict(metrics) -> dict[str, float]:
    return {
        "map50": float(metrics.box.map50),
        "map50_95": float(metrics.box.map),
        "precision": float(metrics.box.mp),
        "recall": float(metrics.box.mr),
    }


def main() -> int:
    if not torch.cuda.is_available():
        raise SystemExit("CUDA GPU is required; select a T4 runtime")
    print(f"gpu={torch.cuda.get_device_name(0)}")

    stage_a = YOLO(str(BUNDLE / "base_dfire_best.pt"))
    stage_a.train(
        data=str(DATASET / "data_stage_a.yaml"),
        epochs=30,
        patience=10,
        imgsz=416,
        batch=32,
        device=0,
        workers=2,
        project=str(RUNS),
        name="smoke-clean-dfire4-stage-a",
        exist_ok=True,
        seed=42,
        deterministic=True,
        rect=False,
        cos_lr=True,
        optimizer="AdamW",
        lr0=0.0005,
        lrf=0.1,
        mosaic=0.0,
        erasing=0.0,
        hsv_h=0.0,
        hsv_s=0.2,
        hsv_v=0.2,
        translate=0.05,
        scale=0.2,
        plots=True,
    )
    stage_a_best = RUNS / "smoke-clean-dfire4-stage-a" / "weights" / "best.pt"
    holdout_model = YOLO(str(stage_a_best))
    holdout_metrics = holdout_model.val(
        data=str(DATASET / "data_stage_a.yaml"),
        split="test",
        imgsz=416,
        conf=0.001,
        device=0,
        plots=True,
        project=str(RUNS),
        name="smoke-clean-holdout-smoke3",
    )

    final_model = YOLO(str(stage_a_best))
    final_model.train(
        data=str(DATASET / "data_final.yaml"),
        epochs=20,
        patience=8,
        imgsz=416,
        batch=32,
        device=0,
        workers=2,
        project=str(RUNS),
        name="smoke-clean-dfire5-final",
        exist_ok=True,
        seed=43,
        deterministic=True,
        rect=False,
        cos_lr=True,
        optimizer="AdamW",
        lr0=0.0003,
        lrf=0.1,
        mosaic=0.0,
        erasing=0.0,
        hsv_h=0.0,
        hsv_s=0.2,
        hsv_v=0.2,
        translate=0.05,
        scale=0.2,
        plots=True,
    )
    final_best = RUNS / "smoke-clean-dfire5-final" / "weights" / "best.pt"
    final = YOLO(str(final_best))
    dfire_metrics = final.val(
        data=str(DATASET / "data_final.yaml"),
        split="val",
        imgsz=416,
        conf=0.001,
        device=0,
        plots=True,
        project=str(RUNS),
        name="smoke-clean-final-dfire-val",
    )
    export_path = Path(final.export(format="ncnn", imgsz=(256, 416)))

    if RESULTS.exists():
        shutil.rmtree(RESULTS)
    RESULTS.mkdir(parents=True)
    shutil.copy2(final_best, RESULTS / "best.pt")
    shutil.copytree(export_path, RESULTS / "best_ncnn_model")
    shutil.copytree(RUNS / "smoke-clean-dfire4-stage-a", RESULTS / "stage_a_run")
    shutil.copytree(RUNS / "smoke-clean-dfire5-final", RESULTS / "final_run")
    shutil.copy2(DATASET / "field_manifest.csv", RESULTS / "field_manifest.csv")
    shutil.copy2(DATASET / "excluded_dfire_labels.txt", RESULTS / "excluded_dfire_labels.txt")
    summary = {
        "gpu": torch.cuda.get_device_name(0),
        "stage_a_holdout_smoke3": metric_dict(holdout_metrics),
        "final_combined_validation": metric_dict(dfire_metrics),
        "export": "best_ncnn_model",
    }
    (RESULTS / "summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    shutil.make_archive(str(RESULTS), "zip", RESULTS)
    print(json.dumps(summary, indent=2))
    print(f"RESULT_ZIP={RESULTS}.zip")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
