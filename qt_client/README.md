# SafeVision (Qt Client)

지능형 통합 화재 관제 시스템의 관제 데스크톱 클라이언트. 라즈베리파이 서버에서 센서/감지 데이터를
받아 표시하고, 액추에이터 수동 제어와 위험 모드 전환/해제를 수행한다.

## 화면 구성

로그인 후 상단 메뉴로 5개 탭을 전환한다. 공장(A~D)은 상단 버튼으로 별도 전환.

| 탭 | 내용 |
|---|---|
| 모니터링 | 카메라 4채널 실시간 영상 + 감지 박스, 감시 제외(ROI) 영역 설정, 센서 수치, 액추에이터 수동 제어, 위험 모드 전환/해제 |
| 이벤트로그 | 서버 DB 조회 결과 목록 + 상세 패널. 날짜/구역/위험도/키워드 필터 |
| 그래프 | 가스·연기 농도 추이. 기간(10분/1시간/6시간/하루) 선택, 경고·위험 발생 시점 마커 |
| 평면도 | 대피 경로 지도. 전광판 클릭 시 각 출구까지의 경로 표시 |
| 도움말 | 화면별 사용법 + 실제 UI를 재현한 미리보기 |

실제 센서·카메라 하드웨어는 **A공장에만** 연결돼 있고, B~D공장은 화면 확인용 시뮬레이션 값이다.

## 폴더 구조

| 경로 | 내용 |
|---|---|
| `dashboard_main.cpp` | 진입점. 폰트 등록, 로그인↔대시보드 전환 |
| `src/core/` | `MainWindow`(셸·탭 전환·서버 시그널 배선), 공용 타입(`ZoneTypes`, `DetectionTypes`, `FloorMapTypes`), 서버 주소(`ServerConfig.h`) |
| `src/pages/` | 탭별 화면 1개 = 파일 1개 (`MonitorPage`, `EventLogPage`, `GraphPage`, `FloorMapPage`, `HelpPage`, `LoginPage`) |
| `src/widgets/` | 페이지가 조립해 쓰는 커스텀 위젯 (상태 패널, 그래프, 감지 오버레이, ROI 편집용 `VideoWidget`, 경고 배너 등) |
| `src/network/` | `ServerLink`(JSON 소켓), `StreamReceiver`(libvlc RTSP 재생) |
| `resources/` | 폰트(한화고딕 2종), 로그인 배경 이미지, `resources.qrc` |

화면 내용은 각 `*Page` 클래스가 소유하고, `MainWindow`는 탭 전환과 서버 시그널 라우팅만 담당한다.

## 개발 환경

- Qt 6.5 이상 (현재 6.11.0 / `mingw_64` 키트로 빌드)
- MinGW (`C:\Qt\Tools\mingw1310_64`) — `dlltool`이 있어야 함
- CMake 3.19 이상 (`C:\Qt\Tools\CMake_64`)
- VLC for Windows (64bit) — `libvlc.dll` 사용

### libvlc 연동 방식

RTSP 저지연 재생에 libvlc를 쓴다(`:network-caching=500`). 공식 SDK를 받지 않고, 설치된
`libvlc.dll`에서 필요한 함수만 뽑아 MinGW 임포트 라이브러리를 빌드 시점에 생성한다
(CMakeLists.txt의 `dlltool` 커스텀 커맨드). 헤더도 필요한 선언만 담은
`src/network/vlc/libvlc_min.h`를 직접 유지한다.

**libvlc 함수를 새로 쓰려면 CMakeLists.txt의 `file(WRITE ... EXPORTS ...)` 목록에 심볼 이름을
직접 추가해야 한다** — 안 그러면 링크 에러가 난다.

## 빌드 방법

Qt Creator에서 `qt_client/CMakeLists.txt`를 열어 빌드하거나, 커맨드라인에서:

```bash
cmake -S qt_client -B qt_client/build -G "MinGW Makefiles"
cmake --build qt_client/build
```

VLC를 기본 경로(`C:/Program Files/VideoLAN/VLC`)가 아닌 곳에 설치했다면:

```bash
cmake -S qt_client -B qt_client/build -G "MinGW Makefiles" -DVLC_DIR="D:/VLC"
```

빌드 후 `libvlc.dll`/`libvlccore.dll`은 실행파일 옆으로 자동 복사된다. plugins 폴더는 용량이 커서
복사하지 않고 `VLC_PLUGIN_PATH` 환경변수로 설치 경로를 그대로 가리킨다(`dashboard_main.cpp`).

## 실행

서버(라즈베리파이)와 MediaMTX가 먼저 떠 있어야 한다.

- 로그인: `admin` / `1234` (현재 `LoginPage.cpp`에 하드코딩 — 서버 인증 미연동)
- 5회 실패 시 30초 잠금. "자동 로그인" 체크 시 다음 실행부터 로그인 화면을 건너뜀(`QSettings`)
- 로그인 시 서버 TCP 연결을 먼저 확인하고, 실패하면 대시보드로 진입하지 않는다

## 서버 연동

주소는 `src/core/ServerConfig.h` 한 곳에서 관리한다. 라즈베리파이 IP가 바뀌면 여기만 고치면 된다.

```cpp
inline const QString kServerHost = "172.20.32.41";
inline const quint16 kServerPort = 9999;
```

`ServerLink`가 줄 단위 JSON(`\n` 구분)으로 통신한다.

| 방향 | 메시지 타입 |
|---|---|
| 수신 | `sensor`, `detection`, `actuator_status`, `led_matrix_status`, `control_ack`, `emergency_ack`, `set_ignore_regions_ack`, `query_result` |
| 송신 | `control`(수동 제어), `emergency_trigger`/`emergency_clear`, `false_alarm_report`, `set_ignore_regions`(ROI 설정), `query`(DB/설정 조회) |

`query`는 `target`으로 `event_log`/`sensor_log`/`ignore_regions`를 구분하고, `reqId`로 응답을 매칭한다.
`ignore_regions`는 접속 직후 서버가 `reqId` 없이 먼저 push하기도 한다(저장된 ROI 복원용).

### ROI(감시 제외 영역)

채널별로 꼭짓점 4개짜리 다각형을 지정해 감지 대상에서 제외한다. `applyTo`로 화재/연기 감지를
따로 켜고 끌 수 있다. `VideoWidget`에서 편집한 영역은 `set_ignore_regions`로 서버에 반영되고,
`overlapThreshold`(서버 전역 0.5) 기준으로 감지 박스와 겹치는 영역을 판단한다.

### TLS

`ServerConfig.h`의 `kUseTls` 스위치로 평문/TLS를 전환한다. Qt 쪽 구현(인증서 SHA-256 지문 고정
검증 포함, `kServerCertSha256`)은 이미 완성돼 있지만, 서버가 아직 평문 TCP만 지원해서 현재는
`false`로 꺼둔 상태다. 서버 측 TLS가 준비되면 이 값만 `true`로 바꾸고 지문 값을 채우면 되고,
Qt 쪽 재작업은 필요 없다.

## 카메라 영상

MediaMTX가 재배포하는 RTSP를 채널별로 받는다 (인증 없음).

```
rtsp://<MediaMTX_HOST>:8554/cam1 ~ cam4
```

채널별 감시 대상은 고정: `Ch.1 화재감지` / `Ch.2 환기팬` / `Ch.3 가스 밸브` / `Ch.4 사이렌&스피커`.
MediaMTX 주소는 `MainWindow.cpp`의 `kMediaMtxHost`에 있다(서버와 같은 파이라 값은 동일하지만
역할이 달라 상수는 분리해 둠).

## 폰트

`06HanwhaGothicL.ttf`(기본) / `07HanwhaGothicEL.ttf`(굵게) 두 벌을 리소스로 등록해 쓴다.

두 TTF가 **서로 다른 패밀리로 등록돼 있어서** `font-weight:bold`만 지정하면 Qt가 EL을 자동으로
골라주지 않는다. 굵게 써야 하는 자리마다 스타일시트에
`font-family:"hanwhaGothic EL"`을 명시적으로 같이 적어야 한다.

## 현재 상태 / 알려진 제약

- **평면도** — 격자·전광판·출구·경로 데이터는 실제 우리 건물 평면도를 `server/evac_map_tools`(태호)로
  변환한 진짜 결과로 교체됐다(더 이상 손으로 지어낸 예시 아님). 다만 아직 서버 연동 전이라
  `FloorMapPage.cpp`에 하드코딩된 값이다 — Qt↔서버 메시지 규격 확정 후 서버에서 받아오도록 연결 예정
- **서버 TLS** — Qt 쪽 TLS(인증서 지문 고정 포함)는 구현 완료. 서버가 아직 평문 TCP만 지원해서
  `ServerConfig::kUseTls = false`로 꺼둔 상태 — 서버 TLS 준비되면 그 값만 바꾸면 된다
- **이벤트로그 스냅샷** — 상세 패널에 자리만 있고 "스냅샷 없음" 표시. 서버가 감지 시점 프레임을
  저장·전송하는 프로토콜이 필요해 미구현
- **로그인 인증** — 서버 연동 없이 클라이언트에 하드코딩된 계정으로만 동작