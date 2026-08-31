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

## ✅ 해결한 문제

### 현장 연기를 놓치고 회색 구조물을 연기로 오인하던 문제

기존 D-Fire 기반 모델은 학습 환경과 다른 세트장의 옅은 연기를 놓치고, 회색 벽과
고정 구조물을 연기로 판단했다. 채널 확대 화면에서 얻은 좌표를 전체 화면 라벨에
잘못 적용한 데이터와 연기 주변 물체까지 포함한 박스를 제거한 뒤, 실제 카메라 영상의
연기 프레임을 다시 라벨링했다. 무연기 채널과 오탐 구조물은 hard negative로 추가해
Round 8 모델을 재학습했다. 운영에서는 원본 비율을 유지한 416×256 letterbox,
class 0(smoke), confidence 0.40을 사용하고 최초 양성 1회에 확정하되 결과를 최대
5초 유지해 공유 worker의 채널 순회 사이에도 박스가 끊기지 않도록 해결했다.

---

## 🔗 참고

- 상위 감지 개요 — [../README.md](../README.md)
- 운영 모델 보고서 — [../training/ROUND8_BALANCED_FIELD_REPORT_KO.md](../training/ROUND8_BALANCED_FIELD_REPORT_KO.md)
- CPU 최적화 — [README_CPU_OPTIMIZATION.md](README_CPU_OPTIMIZATION.md)
