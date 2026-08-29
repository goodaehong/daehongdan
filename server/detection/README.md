# detection — 화재 · 연기 · 사람 감지

## 📌 개요

- 카메라 프레임을 받아 화재 · 연기 · 사람을 검출하고 공장 좌표를 붙여 돌려줌
- 화재는 **OpenCV 룰 기반**, 연기는 **NCNN YOLOv8n**, 사람은 카메라 WiseAI 메타데이터
- 서버 본체와 분리되어 단독으로도 실행 가능

---

## ⚙️ 동작

```
프레임 제출 ──▶ [워커 스레드] 검출 ──▶ 결과 조회 ──▶ 박스 + 격자 좌표
```

### 감지 방식

| 대상 | 방식 | 확정 조건 |
| --- | --- | --- |
| 🔥 화재 | OpenCV 룰 — 색 · 밝기 · 움직임 · 형태 | 연속 프레임 누적 |
| 💨 연기 | NCNN YOLOv8n 모델 | 모델 검출 + 움직임 확인 + 연속 누적 |
| 🧍 사람 | 카메라 WiseAI 메타데이터 수신 | 카메라가 판정 |

- 화염은 색·밝기·움직임·형태로 구분 가능 → 룰로 처리
- 연기는 회색 벽 · 수증기와 겹쳐 룰로 안 잡힘 → 학습 모델 사용

### 처리 규격

| 항목 | 값 | 이유 |
| --- | --- | --- |
| 수신 해상도 | 720p | MediaMTX 재배포 프로파일 |
| 분석 해상도 | 640×360 | 720p 그대로는 파이가 4채널을 못 버팀 |
| 연기 입력 | 640×384 | 모델 입력 규격에 맞춰 상하 패딩 |
| 워커 | **1개가 4채널 순차 처리** | 채널마다 스레드를 두면 CPU 포화 |

- 순차 처리라 채널별 결과 갱신 주기가 길어짐 → 직전 박스를 유지하는 처리 포함

### 좌표 변환

```
화염 박스 ──▶ 바닥 접촉점 ──▶ ArUco 변환식 ──▶ 60×60 공장 좌표
```

- 바닥 접촉점을 쓰는 이유 — 불의 실제 위치는 박스 중심이 아니라 바닥에 닿는 지점
- 전광판과 대피경로가 같은 좌표계를 공유

### 감지 제외 영역

- 원본 영상이나 모델 입력을 가리지 않음
- 검출 · 움직임 검증을 마친 **박스와 지정 영역의 겹침 비율**로만 판단 (기준 0.5)
- 활성 영역이 없으면 모든 검출을 그대로 통과
- 화재 / 연기에 따로 적용 가능 → [server/roi/](../roi/)

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `FireDetectionRuntime.cpp/h` | 화재 처리 전체 흐름 — 제출 · 검출 · 좌표 · 결과 보관 |
| `FireAlarmController.cpp/h` | 화재 알람 확정 — 연속 검출 누적 |
| `FlameDetector.cpp/h` | 화염 검출 — 색 · 밝기 · 움직임 · 형태 |
| `SmokeDetectionRuntime.cpp/h` | 연기 처리 전체 흐름 — 추론 · 추적 · 박스 병합 |
| `SmokeDetector.cpp/h` | 연기 모델 로드 · 추론 |
| `GridCoordinateMapper.cpp/h` | 화면 좌표 → 60×60 공장 좌표 변환 |
| `IgnoreRegionFilter.cpp/h` | 감지 제외 영역 적용 |
| `PersonMetadataReceiver.cpp/h` | 카메라 사람 검출 정보 수신 |
| `AppConfig.h` | 임계값 · 추론 간격 · 모델 경로 |
| `DetectionTypes.h` | 공용 타입 |
| `models/` | 연기 모델 파일 |
| `camera_calibration_ch1~4.yml` | 채널별 렌즈 보정값 |
| `aruco_board_config.txt` | 마커 배치 설정 — 시연 장비 기준값이 커밋되어 있음 |

**하위 폴더**

| 폴더 | 내용 |
| --- | --- |
| [`docs/`](docs/) | 설계 · 조사 문서 (좌표 매핑 · CPU 최적화 · 모델 도입 등) |
| [`training/`](training/) | 연기 모델 학습 스크립트 · 리포트 |
| [`tools/`](tools/) | 보정 · 디버그 · 테스트 도구 (서버가 링크하지 않음) |

---

## 🔧 빌드·실행

**필요** — OpenCV 4.5+ · NCNN (소스 빌드)

감지 코어는 서버 빌드에 포함됩니다. 별도 빌드는 필요 없습니다.

```bash
cmake -S server -B server/build -DENABLE_SMOKE_NCNN=ON   # 기본 ON
```

<details>
<summary><b>모델 교체</b></summary>

`AppConfig.h` 의 경로 상수를 바꾸면 됩니다.

```cpp
MODEL_PARAM_PATH = "models/smoke_yolov8n_.../model.ncnn.param";
MODEL_BIN_PATH   = "models/smoke_yolov8n_.../model.ncnn.bin";
```

> ⚠️ 빌드 시 `models/` 폴더가 **통째로** 실행 파일 옆으로 복사됩니다.
> 안 쓰는 모델을 남겨두면 빌드마다 그만큼 복사됩니다.

</details>

**기동 실패 시**

| 로그 | 조치 |
| --- | --- |
| `[연기] 모델 로드 실패` | 경로와 `models/` 복사 여부 확인 |
| NCNN 못 찾음 | `-Dncnn_DIR=/usr/local/lib/cmake/ncnn` 지정 |
| `[좌표] camN 미설정` | 보정 미완료 — 감지는 계속되고 좌표만 안 붙음 → [calib/](../calib/) |

---

## 🛠 문제 해결

### 파이 4채널에서 CPU 포화

| 조치 | 내용 |
| --- | --- |
| 분석 해상도 축소 | 720p → 640×360 |
| 워커 단일화 | 채널별 스레드 → 1개가 순차 처리 |
| 화재 검출 내부 최적화 | 색공간 분리·마스크·커널을 1회만 계산 후 재사용, 배경 분리를 절반 해상도로, 검사 후보 상한 |

- 부작용 — 아주 작은 움직임에 대한 배경 분리 반응이 약해짐
- 보완 — 원본 해상도 프레임 차분 · 색 · 백색 코어 · 후광 분석은 그대로 유지
- 상세 — [docs/README_CPU_OPTIMIZATION.md](docs/README_CPU_OPTIMIZATION.md)

### 연기를 룰로 못 잡음

- **문제** — 회색 벽 · 수증기와 색·질감이 겹쳐 임계값으로 분리 불가
- **조치** — YOLOv8n 학습 후 NCNN 으로 변환해 탑재
- **보완** — 모델 검출만으로는 오탐이 남아 움직임 확인과 연속 누적을 추가
- 상세 — [docs/README_SMOKE_NCNN.md](docs/README_SMOKE_NCNN.md)

---

## 🔗 참고

- 좌표 매핑 설계 — [docs/ARUCO_COORDINATE_MAPPING_KO.md](docs/ARUCO_COORDINATE_MAPPING_KO.md)
- 사람 메타데이터 — [docs/README_WISEAI_PERSON.md](docs/README_WISEAI_PERSON.md)
- 상위 개요 — [server/README.md](../README.md)
