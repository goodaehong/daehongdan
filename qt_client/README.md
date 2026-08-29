# qt_client — 관제 클라이언트 (Qt)

## 📌 개요

- 라즈베리파이 서버에서 센서·감지 데이터를 받아 표시하는 데스크톱 관제 화면
- 액추에이터 수동 제어 · 위험 모드 전환/해제 수행
- 탭 5개 구성 — 모니터링 · 이벤트로그 · 그래프 · 평면도 · 도움말

---

## ⚙️ 동작

### 화면 구성
로그인 후 상단 메뉴로 5개 탭을 전환한다. 공장(A~D)은 상단 버튼으로 별도 전환.

| 탭 | 내용 |
|---|---|
| 모니터링 | 카메라 4채널 실시간 영상 + 감지 박스, 감시 제외(ROI) 영역 설정, 센서 수치, 액추에이터 수동 제어, 위험 모드 전환/해제 |
| 이벤트로그 | 서버 DB 조회 결과 목록 + 상세 패널. 날짜/구역/위험도/키워드 필터 |
| 그래프 | 가스·연기 농도 추이. 기간(10분/1시간/6시간/하루) 선택, 경고·위험 발생 시점 마커 |
| 평면도 | 대피 경로 지도. 전광판 클릭 시 각 출구까지의 경로 표시 |
| 도움말 | 화면별 사용법 + 실제 UI를 재현한 미리보기 |

실제 센서·카메라 하드웨어는 **A공장에만** 연결돼 있고, B~D공장은 화면 확인용 시뮬레이션 값이다.

### 서버 연동
주소는 `src/core/ServerConfig.h` 한 곳에서 관리한다. 라즈베리파이 IP가 바뀌면 여기만 고치면 된다.

```cpp
inline const QString kServerHost = "172.20.35.185";
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
`ServerConfig.h` 의 `kUseTls` 스위치로 평문/TLS 를 전환한다. 현재 `true`.
인증서는 SHA-256 지문(`kServerCertSha256`)을 고정 검증한다 — 서버 인증서를 새로 만들면
이 값도 같이 갱신해야 한다.

> ⚠️ 서버도 `-DENABLE_TLS=ON` 으로 빌드돼 있어야 합니다. 한쪽만 켜면 연결되지 않습니다.

### 카메라 영상
MediaMTX가 재배포하는 RTSP를 채널별로 받는다 (인증 없음).

```
rtsp://<MediaMTX_HOST>:8554/cam1 ~ cam4
```

채널별 감시 대상은 고정: `Ch.1 화재감지` / `Ch.2 환기팬` / `Ch.3 가스 밸브` / `Ch.4 사이렌&스피커`.
MediaMTX 주소는 `MainWindow.cpp`의 `kMediaMtxHost`에 있다(서버와 같은 파이라 값은 동일하지만
역할이 달라 상수는 분리해 둠).

---

## 📁 주요 파일

| 경로 | 내용 |
| --- | --- |
| `dashboard_main.cpp` | 진입점 — 폰트 등록 · 로그인↔대시보드 전환 |
| `src/core/` | `MainWindow`(셸 · 탭 전환 · 서버 시그널 배선) · 공용 타입 · `ServerConfig.h` |
| `src/pages/` | 탭별 화면 1개 = 파일 1개 (`MonitorPage` · `EventLogPage` · `GraphPage` · `FloorMapPage` · `HelpPage` · `LoginPage`) |
| `src/widgets/` | 페이지가 조립해 쓰는 위젯 (상태 패널 · 그래프 · 감지 오버레이 · ROI 편집 `VideoWidget` · 경고 배너) |
| `src/network/` | `ServerLink`(JSON 소켓) · `StreamReceiver`(libvlc RTSP 재생) |
| `resources/` | 폰트(한화고딕 2종) · 로그인 배경 · `resources.qrc` |

화면 내용은 각 `*Page` 클래스가 소유하고, `MainWindow` 는 탭 전환과 서버 시그널 라우팅만 담당한다.

---

## 🔧 빌드·실행

### 개발 환경
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

### 빌드
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

### 실행
서버(라즈베리파이)와 MediaMTX가 먼저 떠 있어야 한다.

- 로그인: `admin` / `1234` (현재 `LoginPage.cpp`에 하드코딩 — 서버 인증 미연동)
- 5회 실패 시 30초 잠금. "자동 로그인" 체크 시 다음 실행부터 로그인 화면을 건너뜀(`QSettings`)
- 로그인 시 서버 TCP 연결을 먼저 확인하고, 실패하면 대시보드로 진입하지 않는다

---

## 🛠 문제 해결

### 폰트 굵게가 안 먹음
`06HanwhaGothicL.ttf`(기본) / `07HanwhaGothicEL.ttf`(굵게) 두 벌을 리소스로 등록해 쓴다.

두 TTF가 **서로 다른 패밀리로 등록돼 있어서** `font-weight:bold`만 지정하면 Qt가 EL을 자동으로
골라주지 않는다. 굵게 써야 하는 자리마다 스타일시트에
`font-family:"hanwhaGothic EL"`을 명시적으로 같이 적어야 한다.

### 남은 제약
- **로그인 인증** — 서버 연동 없이 클라이언트에 하드코딩된 계정으로만 동작합니다
- **B~D 공장** — 실제 하드웨어는 A공장에만 연결돼 있고, 나머지는 화면 확인용 시뮬레이션 값입니다
- **자동 재접속 없음** — 서버를 껐다 켜면 Qt 를 다시 실행해야 합니다
  (`ServerLink` 의 `disconnected` 에 재시도 타이머 미구현)

---

---

## 🔗 참고

- 서버 쪽 개요 — [server/README.md](../server/README.md)
- 프로젝트 전체 개요 — [README.md](../README.md)
