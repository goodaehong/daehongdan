# calib — ArUco 좌표 보정 관리

## 📌 개요

- 마커 배치 설정을 Qt 에서 받아 저장·복원
- 보정 스크립트 실행을 서버가 관리 — 실행 · 진행 감시 · 취소
- 보정 결과를 서버 재시작 없이 반영
- Homography는 손으로 붙인 마커의 회전·크기 오차에 덜 민감하도록 마커 중심점으로 계산

---

## ⚙️ 동작

```
Qt 좌표 입력 ──▶ 검증 ──▶ 설정 파일 저장
Qt 보정 실행 ──▶ 별도 스레드에서 스크립트 실행 ──▶ 진행 상태 ──▶ 완료 알림
```

### 별도 스레드로 돌리는 이유

- 보정은 수 분 소요 — 요청을 붙잡고 기다릴 수 없음
- 상태만 갱신하므로 **실행 중 취소** 가능

| 상태 | 의미 |
| --- | --- |
| 미설정 | 마커 설정 없음 — 감지는 되고 좌표만 비활성 |
| 실행 중 | 스크립트 동작 중 (취소 가능) |
| 완료 | 좌표 변환식 생성됨 |
| 실패 | 마커 인식 실패 등 — 사유를 Qt 로 전달 |

### 결과 반영과 되돌리기

- 보정 파일이 새로 생기면 카메라 워커가 **버전 변화를 감지해 다시 읽음** (핫스왑)
- 재시작하면 4채널이 전부 끊기므로 이를 피함
- 기존 파일은 타임스탬프 붙여 `.bak` 으로 백업 후 교체 — 보정이 잘못돼도 복구 가능

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `aruco_config.cpp/h` | 좌표 검증 · 설정 저장 · 스크립트 실행·취소 |
| `calib_store.cpp/h` | 채널별 보정 단계 · 실시간 마커 상태 · Qt 조회 응답 |

설정·결과 파일 위치 — `server/detection/`

| 파일 | 저장소 포함 |
| --- | --- |
| `aruco_board_config.txt` | 공통 형식만 커밋됨 — 채널별 `BOARD`·`MARKER`는 설치 시 입력 |
| `camera_calibration_ch1~4.yml` | 커밋됨 |
| `homography_ch*.yml` | 실행 중 생성, 커밋 안 함 |

---

## 🔧 빌드·실행

서버와 Qt는 일반 빌드에 포함됩니다. 보정 도구의 설치·실행 방법은
[detection/tools/calibration/README.md](../detection/tools/calibration/README.md)를 따릅니다.

Qt 상단의 `카메라 좌표 보정`에서 채널별 설정을 저장한 뒤 한 채널씩 보정을 실행합니다.

- 공장·보드·마커 중심 좌표: m
- Qt의 마커 크기: cm (`4cm` 마커는 `4.0` 입력)
- 설정 파일의 마커 크기: m (서버가 `4.0cm`를 `0.04m`로 변환)
- `MODEL_SCALE`: 실제 길이 ÷ 모형 길이. 126cm가 60m를 나타내면 `47.6190476`
- 현재 방식은 마커 중심점 Homography이며 기본 품질 설정은 `QUALITY 4 4 ...`

저장소의 기본 설정에는 현장 `BOARD`·`MARKER`가 없으므로 실제 배치를 반드시 입력해야 합니다.

---

## 🛠 문제 해결

| 증상 | 확인할 것 |
| --- | --- |
| `configuredVisible`이 4 미만 | 카메라에 해당 채널 설정 ID가 4개 이상 보이는지 확인 |
| 중심점 RANSAC 실패 | 마커 좌표가 한 직선에 몰리지 않았는지, `QUALITY 4 4`인지 확인 |
| 계산 시간 초과 | MediaMTX·RTSP 입력과 서버의 스트림 경합 여부 확인 |
| 보정 후 좌표가 비정상 | X/Y 방향, m 단위, `MODEL_SCALE`, 채널 `BOARD` 범위 확인 |
| 예전 결과가 계속 사용됨 | `homography_ch*.yml` 갱신 여부와 재로드 버전 로그 확인 |

최신 코드의 로그는 `marker centres`, 인라이어 ID, 제외 ID와 RMS를 출력합니다.

---

## 🔗 참고

- 보정 스크립트 — [detection/tools/calibration/README.md](../detection/tools/calibration/README.md)
- 좌표 매핑 설계 — [detection/docs/ARUCO_COORDINATE_MAPPING_KO.md](../detection/docs/ARUCO_COORDINATE_MAPPING_KO.md)
- 상위 개요 — [server/README.md](../README.md)
