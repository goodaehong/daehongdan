# detection — 화재 · 연기 · 사람 감지

## 📌 개요

- 카메라 프레임을 받아 화재·연기·사람을 검출하고 좌표를 붙여 돌려줌
- 화재는 **OpenCV 룰 기반**, 연기는 **NCNN YOLOv8n**, 사람은 카메라 WiseAI 메타데이터
- 서버 본체와 분리되어 단독으로도 돌릴 수 있음

---

## ⚙️ 동작

```
프레임 ──▶ submitFrame() ──▶ [워커 스레드] ──▶ poll() ──▶ 박스 + 격자 좌표
```

**화재와 연기를 다른 방식으로 잡는 이유** — 화염은 색·밝기·움직임·형태로 구분이 되지만,
연기는 회색 벽·수증기와 겹쳐서 룰로 안 잡힙니다. 그래서 연기만 학습 모델을 씁니다.

**분석은 640×360 으로 축소해서 합니다.** 수신은 720p 지만 그대로 분석하면 파이 4대 채널을
못 버팁니다. 연기는 NCNN 입력 규격에 맞춰 상하 패딩을 더합니다.

**단일 워커가 4채널을 순차 처리합니다.** 채널마다 스레드를 두면 파이 CPU 가 포화됩니다.
대신 한 채널의 결과 갱신 주기가 길어져, 직전 박스를 유지하는 처리가 들어 있습니다.

**좌표를 격자로 바꿉니다.** ArUco 마커로 잡은 Homography 로 화염 박스의 **바닥 접촉점**을
60×60 공장 좌표로 변환합니다. 전광판과 대피경로가 같은 좌표계를 씁니다.

**감지 제외 영역(ROI)은 검출 후에 적용합니다.** 원본이나 모델 입력을 가리지 않고,
검출·움직임 검증을 마친 박스만 사용자 지정 영역과 비교합니다.

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `FireDetectionRuntime.cpp/h` | 화재 파이프라인 — 제출 · 검출 · 좌표 · 결과 스냅샷 |
| `FireAlarmController.cpp/h` | 화재 알람 확정 (연속 검출 누적) |
| `FlameDetector.cpp/h` | 화염 검출 — 색 · 밝기 · 움직임 · 형태 |
| `SmokeDetectionRuntime.cpp/h` | 연기 파이프라인 — NCNN 추론 · 트랙 · 박스 병합 |
| `SmokeDetector.cpp/h` | NCNN 모델 로드 · 추론 |
| `GridCoordinateMapper.cpp/h` | ArUco → Homography → 60×60 격자 좌표 |
| `IgnoreRegionFilter.cpp/h` | 감지 제외 영역 적용 |
| `PersonMetadataReceiver.cpp/h` | 카메라 WiseAI 사람 메타데이터 수신 |
| `AppConfig.h` | 임계값 · 추론 간격 · 모델 경로 상수 |
| `DetectionTypes.h` | 공용 타입 (박스 · 결과 구조체) |
| `models/` | NCNN 모델 (`.param` · `.bin`) |
| `camera_calibration_ch1~4.yml` | 채널별 렌즈 보정값 |
| `aruco_board_config.txt` | 마커 배치 설정 — 현장마다 다름, 커밋 안 함 |

**하위 폴더**

| 폴더 | 내용 |
| --- | --- |
| `docs/` | 설계·조사 문서 (좌표 매핑 · CPU 최적화 · NCNN 도입 등) |
| `training/` | 연기 모델 학습 스크립트 · 리포트 |
| `tools/` | 보정 · 디버그 러너 · 테스트 (서버가 링크하지 않음) |

---

## 🔧 빌드·실행

**필요** — OpenCV 4.5+ · NCNN (소스 빌드)

감지 코어는 서버 빌드에 포함됩니다. 별도 빌드는 필요 없습니다.

```bash
cmake -S server -B server/build -DENABLE_SMOKE_NCNN=ON   # 기본 ON
```

**모델 교체**

`AppConfig.h` 의 경로 상수를 바꾸면 됩니다.

```cpp
MODEL_PARAM_PATH = "models/smoke_yolov8n_.../model.ncnn.param";
MODEL_BIN_PATH   = "models/smoke_yolov8n_.../model.ncnn.bin";
```

> ⚠️ 빌드 시 `models/` 폴더가 **통째로** 실행 파일 옆으로 복사됩니다.
> 안 쓰는 모델을 남겨두면 빌드마다 그만큼 복사됩니다.

---

## 🛠 문제 해결

- **모델 로드 실패** — `[연기] 모델 로드 실패` 로그. 경로와 `models/` 복사 여부 확인
- **NCNN 못 찾음** — `-Dncnn_DIR=/usr/local/lib/cmake/ncnn` 지정
- **좌표 비활성** — `[좌표] camN 미설정`. ArUco 설정·보정 파일이 없는 상태.
  감지는 계속되고 좌표만 안 붙습니다 → [server/calib/README.md](../calib/README.md)

---

## 🔗 참고

- 좌표 매핑 설계 — [docs/ARUCO_COORDINATE_MAPPING_KO.md](docs/ARUCO_COORDINATE_MAPPING_KO.md)
- CPU 최적화 경위 — [docs/README_CPU_OPTIMIZATION.md](docs/README_CPU_OPTIMIZATION.md)
- 연기 NCNN 도입 — [docs/README_SMOKE_NCNN.md](docs/README_SMOKE_NCNN.md)
- 상위 개요 — [server/README.md](../README.md)
