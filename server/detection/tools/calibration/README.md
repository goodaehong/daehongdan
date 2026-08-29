# calibration — 카메라 렌즈 · 공장 좌표 보정 도구

## 📌 개요

- 감지 결과에 공장 좌표를 붙이기 위한 **2단계 보정** 도구
- ① 렌즈 왜곡 보정 → ② 화면↔공장 좌표 변환식(Homography) 생성
- 실행기가 필요할 때 자동 증분 빌드 — CMake 명령을 직접 칠 일은 거의 없음

---

## ⚙️ 동작

```
① ChArUco 렌즈 보정 ──▶ camera_calibration_chN.yml
② 공장 좌표 등록 + 마커 촬영 ──▶ homography_chN.yml
                                    ▼
                          서버가 기동 시 채널별로 자동 로드
```

### 두 단계로 나눈 이유

| 단계 | 다시 해야 하는 경우 |
| --- | --- |
| ① 렌즈 보정 | 렌즈 · 줌 · 초점 · 해상도/크롭이 바뀔 때 (**카메라 위치가 바뀌어도 불필요**) |
| ② 좌표 변환식 | 카메라 위치 · 각도 · 줌 · 해상도/크롭 · 마커 좌표가 바뀔 때 |

- 렌즈 특성과 설치 위치는 바뀌는 주기가 달라 분리
- ①은 미리보기 창과 키 입력이 필요(데스크톱 또는 X11 포워딩), ②는 GUI 불필요

### 사용 보드

| 항목 | 값 |
| --- | --- |
| 사전 | `DICT_4X4_50` |
| 크기 | 7×5 squares |
| 체스 칸 | 50 mm |
| 내부 마커 | 35 mm |

### 입력 영상

- 기본값은 서버와 동일한 `rtsp://127.0.0.1:8554/camN`
- **서버가 읽는 해상도 · 크롭과 같은 영상**으로 변환식이 생성됨

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `RunCameraCalibration.sh` | ① 렌즈 보정 실행 |
| `SetupArucoChannel.sh` | ② 좌표 등록 + 변환식 생성을 한 번에 |
| `ConfigureArucoChannel.sh` | ②-1 공장/마커 좌표 입력만 |
| `RunFixedHomographyCalibration.sh` | ②-2 변환식 생성만 |
| `RunChannel4Calibration.sh` | 4채널 일괄 실행 |
| `CharucoCalibrator.cpp` | 렌즈 보정 계산 |
| `FixedHomographyCalibrator.cpp` | 좌표 변환식 계산 |
| `BuildCalibrationTools.sh` · `CalibrationCommon.sh` | 빌드 · 공통 함수 |

---

## 🔧 빌드·실행

**필요** — OpenCV · 개발 도구

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev
cd ~/daehongdan
chmod +x server/detection/tools/calibration/*.sh
```

MediaMTX 는 실행한 상태로 두고 서버만 중지합니다.

### ① 렌즈 보정

```bash
./server/detection/tools/calibration/RunCameraCalibration.sh 1
./server/detection/tools/calibration/RunCameraCalibration.sh 2
./server/detection/tools/calibration/RunCameraCalibration.sh 3
./server/detection/tools/calibration/RunCameraCalibration.sh 4
```

| 키 | 동작 |
| --- | --- |
| `Space` | 현재 보드 자세 저장 |
| `U` | 마지막 저장 취소 |
| `C` | 계산하고 결과 저장 |
| `Q` · `Esc` | 종료 |

- 서로 다른 자세 **20~25개**를 저장
- 화면 중앙 · 가장자리 · 모서리와 가까운/먼 거리를 고르게 포함
- 결과 — `server/detection/camera_calibration_chN.yml`

### ② 공장 좌표 등록과 변환식 생성

카메라를 현장 최종 위치에 고정하고 마커를 배치한 뒤 실행합니다.

```bash
./server/detection/tools/calibration/SetupArucoChannel.sh 3
```

두 단계를 분리하려면:

```bash
./server/detection/tools/calibration/ConfigureArucoChannel.sh 3
./server/detection/tools/calibration/RunFixedHomographyCalibration.sh 3
```

<details>
<summary><b>입력 경로가 다를 때</b></summary>

서버 경로가 `camNdet` 으로 바뀌었다면 실행 전에 지정합니다.

```bash
export DHD_CALIBRATION_SOURCE_TEMPLATE='rtsp://127.0.0.1:8554/cam{channel}det'
```

- `{channel}` 은 1~4, `{index}` 는 0~3 으로 치환됩니다
- 두 번째 인수로 RTSP 주소나 영상 파일을 직접 전달할 수도 있습니다

</details>

---

## 🛠 문제 해결

| 증상 | 확인할 것 |
| --- | --- |
| 마커가 4개 미만으로 검출됨 | `DICT_4X4_50`, 인쇄 상태, 초점, 조명, 설정 ID 확인 |
| RANSAC이 계속 실패함 | 마커 중심 좌표가 한 직선에 몰리지 않았는지와 `QUALITY 4 4` 확인 |
| 결과 좌표가 뒤집힘 | 입력한 X/Y 방향과 채널 `BOARD` 범위 확인 |
| 마커 크기가 비정상 | Qt는 cm, 설정 파일은 m 단위인지 확인 |
| 180초 시간 초과 | RTSP 연결과 MediaMTX 상태, 서버의 동일 스트림 점유 여부 확인 |

보정은 한 번에 한 채널만 실행합니다. 다른 채널이 실행 중이면 서버가 요청을 거부합니다.

---

## 🔗 참고

- 생성된 파일은 실행 프로그램이 아니라 **서버가 기동 시 읽는 설정**입니다
  (`camera_worker` 가 채널별로 자동 로드)
- 서버 쪽 보정 관리 — [server/calib/README.md](../../../calib/README.md)
- 좌표 매핑 설계 — [detection/docs/ARUCO_COORDINATE_MAPPING_KO.md](../../docs/ARUCO_COORDINATE_MAPPING_KO.md)
- 상위 개요 — [detection/README.md](../../README.md)
