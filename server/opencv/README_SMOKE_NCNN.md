# Raspberry Pi 4 smoke NCNN integration

## Runtime design

- One `SmokeDetector` loads one NCNN model.
- One `SmokeDetectionRuntime` worker processes all camera channels in round-robin order.
- Every channel accepts at most one frame per second.
- A pending frame is overwritten by the newest frame, so inference never builds a stale queue.
- The 640x360 camera frame is not downscaled. It is letterboxed to 640x384 for NCNN.
- Three consecutive positive smoke results confirm smoke; three misses release it.

Qt/server code should poll `SmokeRuntimeSnapshot` and send at least:

- `smokeDetected`: temporally confirmed boolean
- `smokeScore`: latest raw YOLO confidence
- `resultFrameId`: source frame identifier
- optionally `detection.boxes`: boxes are already mapped to the original 640x360 coordinates

If Qt displays a separate 1280x720 camera stream, multiply 640x360 box coordinates by 2,
or send normalized coordinates. The Pi does not need to resize or transmit the 720p video.

## Model location

Place the Ultralytics NCNN export at:

```text
RP_Fire/
  models/
    smoke_yolo11n_640x384_ncnn_model/
      model.ncnn.param
      model.ncnn.bin
      metadata.yaml
```

The expected export input is `(1, 3, 384, 640)` and the smoke-only output is
`(1, 5, 5040)` (`cx`, `cy`, `w`, `h`, `smoke confidence`). Blob names default to
`in0` and `out0`; change `AppConfig.h` if the generated `.param` uses different names.

## Raspberry Pi build

Install OpenCV and build/install ncnn first. Then point CMake to the directory that
contains `ncnnConfig.cmake`:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SMOKE_NCNN=ON \
  -Dncnn_DIR=/usr/local/lib/cmake/ncnn
cmake --build build -j4
```

The configure output must contain `Smoke detection: NCNN enabled`. If ncnn is not
found, the program deliberately builds a fire-only stub and prints an error at runtime.

## Initial production settings

Edit `smoke_config` in `AppConfig.h`:

- input: 640x384
- inference interval: 1000 ms per channel
- NCNN threads: 3
- confidence: 0.03 (temporary value for the current weak checkpoint)
- confirmation: 3 hits
- release: 3 misses

Measure CPU load and temperature on the actual Pi. If total CPU stays above 85%, first
increase `INFERENCE_INTERVAL_MS` to 1500 or 2000 instead of reducing the image size.
