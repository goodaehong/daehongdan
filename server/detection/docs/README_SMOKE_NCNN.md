# 연기 NCNN 런타임

## 📌 개요

- Raspberry Pi 4에서 카메라 4채널의 연기를 NCNN YOLOv8n 모델로 감지
- 최신 운영 모델과 런타임 설정은 `AppConfig.h`를 단일 기준으로 사용

---

## ⚙️ 동작

- One `SmokeDetector` loads one NCNN model.
- One `SmokeDetectionRuntime` worker processes all camera channels in round-robin order.
- Every channel accepts at most one frame per second.
- A pending frame is overwritten by the newest frame, so inference never builds a stale queue.
- The 640x360 analysis frame is letterboxed to 416x256 for NCNN.
- The first accepted positive result confirms smoke; the last confirmed result is held for up to five seconds.

Qt/server code should poll `SmokeRuntimeSnapshot` and send at least:

- `smokeDetected`: temporally confirmed boolean
- `smokeScore`: latest confidence that survived runtime filtering
- `resultFrameId`: source frame identifier
- optionally `detection.boxes`: boxes are already mapped to the original 640x360 coordinates

If Qt displays a separate 1280x720 camera stream, multiply 640x360 box coordinates by 2,
or send normalized coordinates. The Pi does not need to resize or transmit the 720p video.

## 📁 주요 파일

Place the Ultralytics NCNN export at:

```text
server/detection/models/
  smoke_yolov8n_round8_balanced_field_20260827_416x256_ncnn_model/
    model.ncnn.param
    model.ncnn.bin
    metadata.yaml
```

The export input is `(1, 3, 256, 416)`. The deployed model metadata contains
`0=smoke`, `1=fire`; this runtime reads class 0 only. Blob names default to `in0`
and `out0`; change `AppConfig.h` if the generated `.param` uses different names.

## 🔧 빌드·실행

Install OpenCV and build/install ncnn first. Then point CMake to the directory that
contains `ncnnConfig.cmake`:

```bash
cmake -S server -B server/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SMOKE_NCNN=ON \
  -Dncnn_DIR=/usr/local/lib/cmake/ncnn
cmake --build server/build -j4
```

The configure output must contain `Smoke detection: NCNN enabled`. If ncnn is not
found, the program deliberately builds a fire-only stub and prints an error at runtime.

### 현재 운영 설정

Edit `smoke_config` in `AppConfig.h`:

- input: 416x256
- per-channel submission interval: 1000 ms (actual processing depends on Pi inference speed)
- NCNN threads: 2 on ARM/Raspberry Pi, 3 on other platforms
- confidence: 0.40
- confirmation: 1 hit
- release: 2 misses with a 5-second confirmed-result hold

Measure CPU load and temperature on the actual Pi. If total CPU stays above 85%, first
increase `INFERENCE_INTERVAL_MS` before reducing the image size.

---

## 🛠 문제 해결

| 증상 | 확인할 것 |
| --- | --- |
| `[연기] 모델 로드 실패` | `AppConfig.h` 경로와 실행 파일 옆 `models/` 복사 여부 확인 |
| NCNN을 찾지 못함 | `-Dncnn_DIR=/usr/local/lib/cmake/ncnn` 지정 |
| 박스가 전혀 없음 | BGR→RGB, letterbox 416×256, `in0/out0`, class 0 확인 |
| 처리 지연이 큼 | 실제 Pi 추론 시간과 CPU 온도를 측정하고 제출 주기 조정 |
| 박스가 바로 사라짐 | `CONFIRM_HITS=1`, `RELEASE_HOLD_MS=5000`이 빌드에 반영됐는지 확인 |

---

## 🔗 참고

- 상위 감지 개요 — [../README.md](../README.md)
- 운영 모델 보고서 — [../training/ROUND8_BALANCED_FIELD_REPORT_KO.md](../training/ROUND8_BALANCED_FIELD_REPORT_KO.md)
- CPU 최적화 — [README_CPU_OPTIMIZATION.md](README_CPU_OPTIMIZATION.md)
