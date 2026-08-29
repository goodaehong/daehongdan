# calib — ArUco 좌표 보정

## 📌 개요

- ArUco 마커 설정을 Qt 에서 받아 저장·복원
- 보정 스크립트 실행을 서버가 관리 (실행 · 감시 · 취소)
- 보정 결과를 서버 재시작 없이 반영

---

## ⚙️ 동작

```
Qt 좌표 입력 ──▶ 검증 ──▶ aruco_board_config.txt 저장
Qt 보정 실행 ──▶ 외부 스크립트 fork ──▶ 진행 상태 ──▶ 완료 알림
```

**보정은 수 분이 걸립니다.** 요청을 받고 기다릴 수 없어 별도 스레드에서 실행하고,
상태만 갱신합니다. 그래서 실행 중 취소도 가능합니다.

**결과는 핫스왑됩니다.** 보정 파일이 새로 생기면 카메라 워커가 버전 변화를 감지해
다시 읽습니다. 서버를 재시작하면 4채널이 전부 끊기므로 그걸 피하려는 것입니다.

**기존 파일은 백업 후 교체합니다** (`.bak` 에 타임스탬프). 보정이 잘못돼도 되돌릴 수 있습니다.

| 상태 | 의미 |
| --- | --- |
| 미설정 | 마커 설정이 없음 — 감지는 되고 좌표만 비활성 |
| 실행 중 | 스크립트 동작 중 (취소 가능) |
| 완료 | Homography 생성됨 |
| 실패 | 마커 인식 실패 등 — 사유를 Qt 로 전달 |

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `aruco_config.cpp/h` | 좌표 검증 · 파일 저장 · 스크립트 실행·취소 |
| `calib_store.cpp/h` | 채널별 보정 단계 · 실시간 마커 상태 · Qt 조회 응답 |

설정·결과 파일은 `server/detection/` 에 있습니다
(`aruco_board_config.txt` · `homography_ch*.yml` — 커밋 안 함).

---

## 🔗 참고

- 보정 스크립트 — [detection/tools/calibration/README.md](../detection/tools/calibration/README.md)
- 좌표 매핑 설계 — [detection/docs/ARUCO_COORDINATE_MAPPING_KO.md](../detection/docs/ARUCO_COORDINATE_MAPPING_KO.md)
- 상위 개요 — [server/README.md](../README.md)
