# 화재·연기·ArUco 좌표 디버그 실행기

Qt와 운영 서버 없이 OpenCV 검출 결과를 확인하는 Visual Studio 프로젝트다.

- 기존 화재·연기 검출을 실행한다.
- 마우스로 설정하는 ROI는 화재·연기를 무시할 `Ignore ROI`다.
- 영상 네 점을 클릭하는 좌표 ROI는 사용하지 않는다.
- ArUco 마커로 영상 좌표를 세트장 좌표와 60×60 셀(0..59)로 변환한다.

## Visual Studio에서 빌드

Visual Studio의 **폴더 열기**로 이 `debug` 폴더를 열고 CMake 대상
`DetectionDebugRunner`를 `Release/x64`로 빌드한다. 또는 개발자 PowerShell에서
다음처럼 빌드한다.

```powershell
cmake -S . -B out/build -A x64 -DOpenCV_DIR=C:/opencv/build/x64/vc16/lib
cmake --build out/build --config Release
```

빌드가 끝나면 모델, DLL, `aruco_board_config.txt`가 실행 파일 옆으로 복사된다.

## 실행 명령

PowerShell에서 실행 파일 폴더로 이동한 뒤 다음 형식을 사용한다.

```powershell
.\DetectionDebugRunner.exe "rtsp://사용자:비밀번호@카메라주소/스트림경로" --aruco-config .\aruco_board_config.txt --channel 2
```

저장된 고정 Homography를 운영 방식과 동일하게 시험하려면 렌즈 보정과 고정 파일을
함께 지정한다.

```powershell
.\DetectionDebugRunner.exe "rtsp://사용자:비밀번호@카메라주소/스트림경로" --headless --max-frames 120 --aruco-config .\aruco_board_config.txt --camera-calibration .\camera_calibration_ch3.yml --static-homography .\homography_ch3.yml --channel 3
```

영상 파일도 같은 방법으로 검사할 수 있다.

```powershell
.\DetectionDebugRunner.exe "C:\video\test.mp4" --aruco-config .\aruco_board_config.txt --channel 2
```

`--channel`은 카메라 장비의 물리 채널 번호가 아니라 설정 파일의 논리 채널
번호다. 현재 예제 설정은 채널 2다.

설정 파일 경로를 매번 쓰기 싫으면 `FIRE_ARUCO_CONFIG_PATH` 환경변수로 지정할
수 있다. 아무 경로도 지정하지 않으면 실행 파일 옆의
`aruco_board_config.txt`를 자동으로 읽는다.

## 로그 보는 법

```text
aruco configured=1 valid=1 fresh=1 markers=4/4 inlierCorners=16 rms=1.2px
```

- `configured=1`: 설정 파일과 해당 논리 채널을 읽었다.
- `markers=4/4`: 검출한 마커 네 개가 모두 설정 파일에 등록돼 있다.
- `inlierCorners=16`: RANSAC에서 채택된 마커 모서리 수다.
- `rms`: 영상에서 다시 투영했을 때의 픽셀 오차다.
- `fresh=1`: 현재 사용할 수 있는 Homography가 있다.
- 화재 박스의 `grid=(x,y)`: 최종 60×60 표시 좌표(0..59)다.
- `factory=(x m,y m)`: 설정한 실제 공장 좌표다.
- `grid=invalid`: 마커 보정이 아직 없거나, 불의 바닥점이 BOARD 밖이다.

## Ignore ROI 조작

- 왼쪽 클릭: 꼭짓점 추가
- 오른쪽 클릭: 마지막 꼭짓점 취소
- `R`: 현재 다각형을 Ignore ROI로 적용
- `U`: 마지막 Ignore ROI 삭제
- `C`: 모든 Ignore ROI 삭제
- `D`: 화재 분석 상세 표시
- `Space`: 일시정지/재생
- `Q` 또는 `Esc`: 종료

Ignore ROI는 ArUco 좌표보정과 별개다. 따라서 좌표보정 방식을 바꿔도 화재·연기
무시 구역 기능은 그대로 사용할 수 있다.
