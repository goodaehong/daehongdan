# calib — ArUco 좌표 보정 관리

## 📌 개요

- 마커 배치 설정을 Qt 에서 받아 저장·복원
- 보정 스크립트 실행을 서버가 관리 — 실행 · 진행 감시 · 취소
- 보정 결과를 서버 재시작 없이 반영

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
| `aruco_board_config.txt` | 시연 장비 기준값 커밋됨 (백업본은 제외) |
| `camera_calibration_ch1~4.yml` | 커밋됨 |
| `homography_ch*.yml` | 실행 중 생성, 커밋 안 함 |

---

## 🔗 참고

- 보정 스크립트 — [detection/tools/calibration/README.md](../detection/tools/calibration/README.md)
- 좌표 매핑 설계 — [detection/docs/ARUCO_COORDINATE_MAPPING_KO.md](../detection/docs/ARUCO_COORDINATE_MAPPING_KO.md)
- 상위 개요 — [server/README.md](../README.md)
