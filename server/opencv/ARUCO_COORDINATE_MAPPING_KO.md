# ArUco 설치 보정과 고정 공장 좌표 변환

기존 영상에서 네 점을 클릭해 저장하던 좌표 ROI는 사용하지 않는다. 화재·연기
검출과 Ignore ROI는 기존 코드를 그대로 사용하고, 화재 박스의 아래 중앙점 좌표만
ArUco 기반 `GridCoordinateMapper`가 계산한다.

## 설치 전에 알아야 하는 값

1. 실제 공장 전체 좌표 범위: `FACTORY minX minY maxX maxY`
2. 해당 채널이 담당하는 실제 공장 범위: `BOARD`
3. 축소 모형 배율: `MODEL_SCALE`
4. 사용할 ArUco dictionary와 각 마커 ID
5. 각 마커 중심의 실제 공장 `X,Y`
6. 출력한 검은 정사각형의 물리적인 한 변 길이
7. 폼보드 기준 마커 회전각: 같은 방향이면 모두 `0`
8. 해당 영상을 논리 채널 몇 번으로 사용할지
9. 출력 격자: 60x60 전체를 사용하는 0 기반 좌표 `0..59`

카메라 높이는 바닥 평면 Homography 계산에 필요하지 않다. 렌즈 왜곡이 큰
카메라에서는 별도의 렌즈 보정을 먼저 적용하면 가장자리 오차가 줄어든다.

## 설정 파일

기본 파일은 `aruco_board_config.txt`이다. 다른 파일은 환경변수로 지정한다.

```bash
FIRE_ARUCO_CONFIG_PATH=/path/to/aruco_board_config.txt
```

형식은 다음과 같다.

```text
VERSION 1
DICTIONARY DICT_4X4_50
GRID 60 0 59
FACTORY 0.00 0.00 60.00 60.00
MODEL_SCALE 50
QUALITY 4 12 2.0 1500 1 0.45
BOARD 4 30.00 0.00 60.00 30.00
MARKER 4 0 31.50 1.50 0.04 0
```

`QUALITY` 값은 차례로 최소 보이는 마커 수, 최소 RANSAC 인라이어 모서리 수,
최대 재투영 RMS 픽셀, 마지막 정상 Homography 유지 시간(ms), 갱신 프레임 간격,
화면 흔들림 완화 계수다.

`FACTORY`는 실제 공장 전체의 최소/최대 XY(m)다. 최종 60×60 좌표는 항상 이
범위를 기준으로 계산하므로 서로 다른 채널 결과가 같은 공장 좌표계에 놓인다.

`MODEL_SCALE`은 `실제 공장 길이 / 모형 길이`다. 현재 60cm 세트장이 실제 30m
구역을 나타내므로 `30 / 0.60 = 50`이다. 실제 공장 바닥에 마커를 직접 설치하는
경우에는 `MODEL_SCALE 1`을 사용한다.

`BOARD`는 논리 채널과 그 채널이 담당하는 실제 공장 최소/최대 XY(m)다.

`MARKER`는 논리 채널, ID, 실제 공장 중심 X(m), 실제 공장 중심 Y(m), 출력한
마커 검은 정사각형의 물리 한 변(m), 시계방향 회전각이다.
프로그램은 마커 중심만 쓰지 않고 검출된 네 모서리를 모두 사용한다. 따라서 직사각형
꼭짓점 한 장이 안 보여도 나머지 세 꼭짓점과 중앙 마커로 계산할 수 있다.

## 설치 보정 흐름

```text
카메라 프레임
 -> ArUco ID와 네 모서리 검출
 -> 설정 파일의 실제 네 모서리 좌표 조회
 -> RANSAC Homography 계산과 재투영 오차 검사
 -> 채널별 homography_chN.yml 저장
```

프로젝트 루트에서 `./SETUP_ARUCO_CHANNEL.sh N`을 실행하면 좌표 입력 후 고정값
생성까지 연속으로 실행된다. 여기서 `N`은 1~4 채널이다. 두 단계를 분리해야 할
때만 `./CONFIGURE_ARUCO_CHANNEL.sh N`과 `./CALIBRATE_FIXED_HOMOGRAPHY.sh N`을
순서대로 사용한다.
설치 보정 중에는 화재·연기 서버를 정지하고, 설정한 마커 중 적어도 네 개가
안정적으로 보이게 한다.

## 운영 연동 흐름

```text
서버 담당 코드에서 채널별 FireDetectionRuntime 생성
 -> loadArucoBoardConfiguration(...)
 -> loadCameraCalibration(camera_calibration_chN.yml, ...)
 -> loadStaticHomography(homography_chN.yml, ...)
 -> 화재 박스 아래 중앙점을 저장된 Homography로 실제 공장 XY(m)로 변환
 -> FACTORY 전체 범위를 기준으로 60x60 셀(0..59) 변환
```

고정 파일을 성공적으로 불러온 운영 중에는 ArUco를 매 프레임 검출하지 않는다.
따라서 검증 후 바닥 마커를 제거할 수 있다. 카메라 위치·각도·줌, 영상 크롭,
렌즈 보정 또는 마커 실제 좌표가 바뀌면 고정 Homography를 다시 만들어야 한다.

Linux 실행기는 기본적으로 현재 운영 서버와 동일한
`rtsp://127.0.0.1:8554/camN`을 읽는다. 서버가 `camNdet` 등의 다른 경로를 쓰면
다음처럼 정확히 같은 경로를 지정한다.

```bash
export DHD_CALIBRATION_SOURCE_TEMPLATE='rtsp://127.0.0.1:8554/cam{channel}det'
./SETUP_ARUCO_CHANNEL.sh 3
```

OpenCV `DetectionBox`에는 `gridX`, `gridY`, `displayRadiusCells`와 함께
`factoryXMetres`, `factoryYMetres` 및 상세 추정값이 준비된다. 서버 담당자가
필요한 필드를 Qt 프로토콜에 연결한 뒤 디스플레이는
`gridValid=true`인 화재에 대해 `gridX/gridY`를 중심, `displayRadiusCells`를
셀 단위 원 반경으로 사용하면 된다. 대표 위치, 평활화 대표 위치, 위치 이력,
바닥 접촉 구간과 그룹 개수도 전송한다.

`displayRadiusCells`는 최근 5회 중앙값과 3회 변경 확인을 거쳐 안정화된다.
따라서 박스가 한두 프레임 흔들려도 반경이 `1↔2↔3`으로 즉시 깜빡이지 않으며,
실제 크기 변화가 계속되면 새 반경으로 갱신된다.

면적 추세, 최초 발생 위치, 이동 방향·속도, 확산량·확산 방향은 공개
`DetectionBox`에서 제거했다. 관련 상태와 면적 변화율 계산은 화재 위치 평활화와
내부 추적 판단을 위해 `FireDetectionRuntime` 내부에만 남아 있다.

설치 보정 시 마커가 부족하거나 오차가 크면 새 Homography를 거부한다. 운영 서버는
고정 파일의 채널·마커 좌표 설정·렌즈 보정 서명이 현재 값과 다르면 로드를 거부하고
`gridPositionValid=false`로 좌표를 보내지 않는다. 화재·연기 감지는 계속 동작한다.

## Ignore ROI

화재·연기를 무시할 다각형 ROI는 `IgnoreRegionFilter`가 계속 담당한다. 이 ROI는
영상 정규화 좌표이며 ArUco 보정과 독립적이다. 좌표보정 방식이 바뀌어도 사용자가
설정한 무시 구역은 그대로 적용된다.
