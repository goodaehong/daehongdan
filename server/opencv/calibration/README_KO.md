# Raspberry Pi 카메라·좌표 보정

보정 프로그램은 C++/OpenCV이며 Linux에서 CMake로 빌드한다. 저장소의 실행기는
필요할 때 자동으로 증분 빌드하므로 일반적으로 CMake 명령을 직접 입력할 필요가 없다.

## 준비

Raspberry Pi OS에서 개발 도구와 OpenCV를 설치한다.

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libopencv-dev
cd ~/daehongdan
chmod +x ./*.sh server/opencv/calibration/*.sh
```

MediaMTX는 실행한 상태로 두고 화재·연기 서버만 중지한다. 기본 입력은 현재
`server_main.cpp`와 동일한 `rtsp://127.0.0.1:8554/camN`이다. 따라서 실제 서버가
읽는 해상도·크롭과 같은 영상으로 Homography가 생성된다.

서버 경로가 `camNdet`으로 바뀌었다면 실행 전에 다음 값을 지정한다.

```bash
export DHD_CALIBRATION_SOURCE_TEMPLATE='rtsp://127.0.0.1:8554/cam{channel}det'
```

`{channel}`은 1~4, `{index}`는 0~3으로 치환된다. 명령의 두 번째 인수로 정확한
RTSP 주소나 영상 파일을 직접 전달할 수도 있다.

## 1. ChArUco 렌즈 보정

사용 보드는 `DICT_4X4_50`, 7×5 squares, 체스 칸 50 mm, 내부 마커 35 mm다.
렌즈·줌·초점·해상도/크롭이 같은 동안에는 카메라 위치가 바뀌어도 이 단계는 다시
하지 않아도 된다.

```bash
./CALIBRATE_CAMERA.sh 1
./CALIBRATE_CAMERA.sh 2
./CALIBRATE_CAMERA.sh 3
./CALIBRATE_CAMERA.sh 4
```

이 도구는 미리보기 창과 키 입력을 사용하므로 Raspberry Pi 데스크톱에서 실행하거나
X11 포워딩을 사용해야 한다.

- `Space`: 현재 보드 자세 저장
- `U`: 마지막 저장 취소
- `C`: 계산하고 YAML 저장
- `Q` 또는 `Esc`: 종료

서로 다른 자세 20~25개를 저장하고, 화면 중앙·가장자리·모서리와 가까운/먼 거리를
고르게 포함한다. 결과는 `server/opencv/camera_calibration_chN.yml`에 저장된다.

## 2. 공장 좌표 등록과 고정 Homography 생성

카메라를 현장 최종 위치에 고정하고 마커를 배치한 뒤 실행한다.

```bash
./SETUP_ARUCO_CHANNEL.sh 3
```

한 명령이 실제 공장/마커 좌표 입력과 `homography_ch3.yml` 생성을 연속 수행한다.
두 단계를 분리하려면 다음을 사용한다.

```bash
./CONFIGURE_ARUCO_CHANNEL.sh 3
./CALIBRATE_FIXED_HOMOGRAPHY.sh 3
```

Homography 생성은 GUI가 필요 없다. 카메라 위치·각도·줌·스트림 해상도/크롭 또는
마커 좌표가 바뀌면 이 단계는 다시 해야 한다.

생성된 파일은 실행되는 프로그램이 아니다. 운영 서버가 시작될 때 채널별
`camera_calibration_chN.yml`과 `homography_chN.yml`을 명시적으로 로드해야 한다.
현재 `aruco-files-only` 브랜치는 서버 파일을 `main`으로 원복했으므로 자동 로드
연결은 서버 담당자가 추가한다.
