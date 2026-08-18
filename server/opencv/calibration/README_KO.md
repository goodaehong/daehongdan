# 채널 4 ChArUco 렌즈 보정

사용 보드 규격:

- A3 가로 420 x 297 mm, 실제 크기 100% 출력
- `DICT_4X4_50`
- 7 x 5 squares
- 체스 칸 50 mm
- 내부 ArUco 35 mm
- 마커 17개, ChArUco 코너 24개

화재·연기 서버를 중지한 뒤 PowerShell에서 실행한다. CMD 실행기가 현재
프로세스에만 실행 정책 우회를 적용하므로 Windows 정책을 영구 변경하지 않는다.

```powershell
cd C:\Users\3-19\Desktop\yolo_test\daehongdan_aruco_mapping\server\opencv\calibration
.\RunChannel4Calibration.cmd
```

또는 프로젝트 최상위 폴더의 `CALIBRATE_CHANNEL4.cmd`를 더블클릭한다.

다른 채널은 공통 실행기에 채널 번호를 전달한다.

```powershell
.\RunCameraCalibration.cmd 1
.\RunCameraCalibration.cmd 2
.\RunCameraCalibration.cmd 3
.\RunCameraCalibration.cmd 4
```

채널 번호를 생략하면 실행기가 1~4 중 하나를 입력받는다. 결과는 자동으로
`camera_calibration_ch1.yml`부터 `camera_calibration_ch4.yml`에 각각 저장된다.

카메라 비밀번호는 실행할 때만 입력하며 파일에 저장하지 않는다.

미리보기 키:

- `Space`: 현재 보드 자세 저장
- `U`: 마지막 저장 취소
- `C`: 보정 계산 후 YAML 저장
- `Q` 또는 `Esc`: 종료

카메라와 줌·초점은 고정하고 보드를 중앙, 네 가장자리, 네 모서리,
가까운 위치와 먼 위치에서 좌우·상하로 기울인다. 한 자세에서 여러 장을
저장하지 말고 서로 다른 자세 20~25장을 저장한다. `C`는 촬영 키가 아니라
마지막 계산·저장 키이므로 20장 이상 모은 뒤 한 번만 누른다.

계산기는 정사각형 픽셀을 전제로 `fx=fy`를 고정하고 불안정한 `k3` 항을
사용하지 않는다. 촬영점이 화면 가로·세로의 55% 이상을 덮지 않거나 보드
크기 변화가 부족하거나 렌즈 결과가 비정상이면 기존 YAML을 덮어쓰지 않는다.

결과 파일은 다음 위치에 생성된다.

```text
server/opencv/camera_calibration_ch4.yml
```

운영 서버는 시작할 때 이 파일을 자동으로 찾는다. 파일이 없으면 기존처럼
렌즈 보정 없이 화재 검출과 ArUco Homography를 수행한다. 파일이 있으면
ArUco 모서리와 화재 바닥점만 `undistortPoints`로 보정하며 영상 전체를
펼치지 않으므로 라즈베리파이 부하는 작다.

주의: ChArUco 보드에는 공장 좌표용 바닥 마커와 같은 ID 0~3이 포함된다.
보정 중에는 운영 화재 서버를 중지하고, 보정 완료 후 ChArUco 보드를
화면에서 제거한 다음 운영 서버를 시작해야 한다.

## 공장 좌표와 고정 Homography 등록

렌즈 보정과 별개로, 시연 현장의 실제 공장/마커 좌표는 프로젝트 루트에서 다음
한 명령으로 등록한다.

```bat
SETUP_ARUCO_CHANNEL.cmd 3
```

채널 번호는 1~4로 바꿀 수 있다. 좌표 입력이 끝나면 카메라 비밀번호를 입력받고
고정 Homography를 바로 생성한다. 기존 좌표가 바뀌면 예전 Homography는 자동으로
`.stale.bak`으로 이동되어 운영 서버가 잘못 불러오지 않는다.
