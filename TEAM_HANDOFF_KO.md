# ArUco 좌표 변환 조원 인수인계

## 현재 완료된 범위

- 채널 1~4 렌즈 보정: `server/opencv/camera_calibration_chN.yml`
- 시연 현장에서 실제 공장 좌표를 입력하는 1단계 설치 명령 제공
- 서버 연동에 사용할 채널별 설정·렌즈 보정·고정 Homography 로드 API
- 마커 종이가 서로 90도씩 다르게 놓여도 설치 보정 중 방향 자동 선택
- 화재 박스 아래 중앙점에서 실제 공장 XY와 60×60 셀(0..59) 계산
- 화재 바닥 폭·추정 면적·디스플레이 반경 계산
- 디스플레이 반경 최근 5회 중앙값 + 3회 변경 확인 평활화

화재 크기는 박스 폭을 바닥 평면에 투영한 추정치다. 실제 화염의 정확한 바닥
면적이나 높이라고 표현하면 안 된다.

## 디스플레이 연동값

OpenCV `DetectionBox`에서 다음 값이 준비된다. 서버 담당자가 Qt 프로토콜에
연결할 때 디스플레이는 화재 항목에서 다음 세 값을 사용하면 된다.

```text
gridX: 0..59 범위의 표시 중심 X
gridY: 0..59 범위의 표시 중심 Y
displayRadiusCells: 표시할 원의 반경(셀), 최소 1
```

`gridX/gridY`는 화재 박스의 기하학적 중앙이 아니라 박스 아래 중앙점을 바닥에
투영한 발화 위치 추정값이다. 디스플레이에서는 이 셀을 원의 중심으로 사용한다.
`gridValid=false`이면 좌표와 크기를 사용하지 않는다. 화면 밖으로 나가는 원 부분은
각 축 `0..59`에서 잘라 그린다.

## 시연 현장 좌표 등록

저장소에는 채널별 시험 좌표와 고정 Homography를 넣지 않았다. 시연 현장에서
서버를 정지하고 필요한 각 채널에 대해 다음 한 명령을 실행한다.

```bash
./SETUP_ARUCO_CHANNEL.sh 3
```

실제 공장 전체 범위, 축척, 채널 범위, 마커 ID별 실제 공장 중심 XY를 입력하면
카메라 연결과 고정 Homography 저장까지 이어진다. 채널 범위와 신규 마커 좌표는
기본값 없이 필수 입력이다. 완료 후 실제로 알고 있는 바닥 검증점 여러 곳에서 셀
오차를 확인한 뒤 마커를 제거한다. 다른 채널은 마지막 숫자만 바꿔 반복한다.

## 고정 파일 사용 조건

`homography_chN.yml`은 생성 당시 카메라 위치·각도·줌과 입력 스트림 전용이다.
다음 중 하나라도 바뀌면 `./SETUP_ARUCO_CHANNEL.sh N`을 다시 실행한다.

- 카메라 위치 또는 각도
- 광학/디지털 줌
- 영상 크롭·여백·손떨림 보정
- 렌즈 보정 파일
- 마커 실제 공장 좌표

같은 시연 장비와 고정 설치를 조원들이 함께 사용한다면 채널별 렌즈 보정과
Homography 파일을 저장소에 포함해도 된다. 다른 현장에서도 쓰는 범용 저장소라면
이 두 종류의 현장 파일은 별도 배포하고 현장에서 다시 생성한다.

## 카메라 스트림

RTSP 중계는 최신 `main`의 `server/streaming/mediamtx.yml`과
`run-mediamtx.sh` 구성을 그대로 사용한다. WiseAI 사람 메타데이터가 필요하면 서버
실행 환경에 다음 값을 별도로 설정한다.

```text
FIRE_PERSON_RTSP_TEMPLATE=rtsp://user:URL인코딩암호@camera:554/{channel}/profile2/media.smp
```

카메라 암호가 기존 소스나 공유 대화에 노출된 적이 있으므로 공개 저장소에 올리기
전 장비의 암호를 변경하는 것을 권장한다.

## GitHub에 올리기 전 확인

- 실제 카메라 암호 또는 인증정보가 검색되지 않는지 확인
- 현장 인증정보, 빌드 결과, DB, 스냅샷을 새로 커밋하지 않기
- 루트와 `server/opencv/calibration/`의 `.sh` 실행 파일을 커밋하기
- `aruco_board_config.txt`, 채널별 렌즈 보정 및 Homography 포함 여부를 팀에서 결정
- `ArucoGridMapper tests passed` 확인

## 빌드 환경 주의

보정 도구는 Linux/Raspberry Pi용 CMake와 Bash 실행기로 정리했다. 실행기는
필요할 때 `server/opencv/calibration/out/build/linux-release`에 증분 빌드한다.
ChArUco 렌즈 보정은 미리보기 창이 필요하므로 Raspberry Pi 데스크톱 또는 X11
포워딩 환경에서 실행한다. 고정 Homography 생성은 화면 없이 실행할 수 있다.

기본 입력은 현재 `server_main.cpp`와 동일한 MediaMTX 경로
`rtsp://127.0.0.1:8554/camN`이다. 서버 입력 경로가 바뀌면
`DHD_CALIBRATION_SOURCE_TEMPLATE`을 같은 경로로 바꿔야 한다. 좌표 보정 중에는
화재·연기 서버만 중지하고 MediaMTX는 실행해 둔다.

`server/server_main.cpp`, `server/qt_link.h`, `server/qt_link.cpp`와 Qt 수신 코드는
최신 `main` 상태를 유지하며 이 브랜치에서 감지 결과를 연결하지 않는다.
서버 담당자가 머지 후 연동한다.

이 브랜치는 최신 `main`의 ROI 서버 연동, Qt 프로토콜 및 전광판 기준을 유지한다.
오디오는 실장비 검증된 `server/audio/speaker_alert*`로 통일했다. 면적 추세·최초
위치·이동/확산 정보는 평활화와 내부 추적 계산에 남아
있지만 Qt JSON으로는 전송하지 않는다.
