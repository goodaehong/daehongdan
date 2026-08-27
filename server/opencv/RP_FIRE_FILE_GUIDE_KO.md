# RP_Fire 파일별 상세 설명서

이 문서는 `RP_Fire` 프로젝트의 전체 구조와 각 파일의 역할을 설명한다.
특히 이번에 추가한 한화비전 `PNM-C16083RVQ` WiseAI 사람 객체 좌표 수신
기능, 기존 OpenCV 화염 감지, NCNN 연기 감지의 연결 관계를 중심으로
정리한다.

실제 C++ 구문과 함수 내부를 따라가며 읽으려면
[`RP_FIRE_CODE_GUIDE_KO.md`](RP_FIRE_CODE_GUIDE_KO.md)를 함께 참고한다.

> 중요: `RP_Fire`와 `daehongdan`은 서로 다른 프로젝트다. 이 문서에서
> 설명하는 변경 사항은 `RP_Fire`에만 적용되어 있다.

---

## 1. 전체 실행 구조

프로그램은 카메라 IP 하나를 입력받고, 동일한 카메라의 4개 센서에 다음
RTSP 주소로 연결한다.

```text
CH1: /0/profile10/media.smp
CH2: /1/profile10/media.smp
CH3: /2/profile10/media.smp
CH4: /3/profile10/media.smp
```

각 채널의 처리 흐름은 다음과 같다.

```text
PNM-C16083RVQ
 ├─ 영상 RTSP ──> CameraStream
 │                 ├─> FireDetectionRuntime ──> OpenCV 화염 감지
 │                 ├─> SmokeDetectionRuntime ─> 공유 NCNN 연기 모델
 │                 └─> FireView ──────────────> 4분할 화면
 │
 └─ WiseAI XML ─> PersonMetadataReceiver
                   ├─> Person/Human 분류 추출
                   ├─> x, y, width, height 변환
                   ├─> FireView 초록색 사람 박스
                   └─> 콘솔 좌표 출력
```

화염, 연기, 사람 감지 방법은 서로 다르다.

| 기능 | 처리 위치 | 방식 | AI 모델 파일 |
|---|---|---|---|
| 화염 | Raspberry Pi/PC | OpenCV 색상·움직임·추적 | 필요 없음 |
| 연기 | Raspberry Pi/PC | NCNN YOLO 모델 | `.param`, `.bin` 필요 |
| 사람 | 한화 카메라 내부 | WiseAI 결과 XML 수신 | Pi에 사람 모델 불필요 |

---

## 2. 이번 WiseAI 작업에서 새로 만든 파일

### 2.1 `PersonMetadataReceiver.h`

WiseAI 메타데이터 수신기의 외부 인터페이스를 선언한다.

주요 함수:

```cpp
bool start(const std::string& rtspUrl);
void stop();
PersonMetadataFrame snapshot(const cv::Size& videoFrameSize) const;
```

- `start()`  
  지정된 채널의 RTSP 주소를 사용하여 메타데이터 수신 스레드를 시작한다.
  영상 디코딩 스레드와는 별도이므로 메타데이터 연결이 실패해도 영상,
  화염 감지, 연기 감지는 계속 실행된다.

- `stop()`  
  FFmpeg 메타데이터 프로세스와 수신 스레드를 종료한다.

- `snapshot()`  
  가장 최근에 수신한 사람 목록을 복사해서 반환한다. 카메라 좌표를
  `videoFrameSize` 기준의 실제 픽셀 좌표로 변환한 결과가 들어 있다.

클래스 내부 구현은 `Impl`로 숨겨져 있다. 헤더를 사용하는 다른 파일이
Windows API나 Linux 프로세스 API를 직접 알 필요가 없도록 하기 위한
PImpl 구조다.

### 2.2 `PersonMetadataReceiver.cpp`

WiseAI 사람 객체 좌표 수신의 실제 구현 파일이다.

#### A. XML 태그 검색

다음과 같이 XML namespace 접두사가 달라도 같은 태그로 처리한다.

```xml
<tt:Frame>
<Frame>
<ns:Frame>
```

주요 내부 함수:

- `findOpeningTag()`  
  `Frame`, `Object`, `BoundingBox`, `Type` 등의 시작 태그를 찾는다.

- `findClosingTag()`  
  namespace와 관계없이 대응하는 닫는 태그를 찾는다.

- `extractAttribute()`  
  `ObjectId`, `left`, `right`, `Likelihood`와 같은 XML 속성을 읽는다.

- `extractNumber()`  
  속성 문자열을 안전하게 `double` 값으로 변환한다.

별도 XML 라이브러리를 사용하지 않으므로 Raspberry Pi에 추가 XML
패키지를 설치할 필요가 없다.

#### B. 사람 클래스 판별

`isPersonClass()`는 다음 명칭을 사람으로 취급한다.

```text
Person
Human
HumanBody
Body
```

대소문자, 공백, `_`, `-`, namespace 접두사는 제거해서 비교한다.

`containsPersonClass()`는 서로 다른 ONVIF 버전을 모두 고려한다.

```xml
<tt:Type Likelihood="0.91">Human</tt:Type>
```

또는:

```xml
<tt:ClassCandidate>
  <tt:Type>Human</tt:Type>
  <tt:Likelihood>0.91</tt:Likelihood>
</tt:ClassCandidate>
```

한화 펌웨어가 `SimpleItem`으로 분류값을 보내는 경우도 확인한다.

#### C. 박스 좌표 추출

`parseFrame()`은 각 `Frame` 안의 `Object`를 순회한다.

처리 순서:

1. 객체 클래스가 사람인지 확인한다.
2. `BoundingBox`의 `left`, `top`, `right`, `bottom`을 읽는다.
3. `Likelihood`를 신뢰도로 저장한다.
4. `ObjectId`가 있으면 함께 저장한다.
5. 사람 이외의 차량, 얼굴, 번호판 객체는 제외한다.

#### D. 좌표계 변환

카메라 펌웨어에 따라 좌표가 다음 세 형식 중 하나일 수 있다.

1. ONVIF 정규화 좌표 `-1.0 ~ 1.0`
2. 영상 정규화 좌표 `0.0 ~ 1.0`
3. 원본 픽셀 좌표

`toPixelBox()`가 형식을 판별하여 최종적으로 다음 형태로 변환한다.

```text
x      = 왼쪽 위 X
y      = 왼쪽 위 Y
width  = 박스 너비
height = 박스 높이
```

640×360 영상이라면 최종 박스는 영상 범위를 벗어나지 않도록 잘린다.
화면 표시 시 박스가 사람 몸에 너무 딱 붙지 않도록 `AppConfig.h`에
정의된 작은 여백도 추가된다.

#### E. FFmpeg 메타데이터 전용 연결

OpenCV `VideoCapture`는 RTSP 영상은 읽지만 ONVIF XML 데이터 트랙을
외부로 전달하지 않는다. 따라서 FFmpeg를 별도 프로세스로 실행한다.

핵심 옵션:

```text
-allowed_media_types data
-map 0:d:0?
-codec copy
-f data
```

- `data` 트랙만 요청한다.
- 영상을 다시 디코딩하지 않는다.
- XML 패킷을 그대로 표준 출력 파이프로 복사한다.
- 메타데이터 트랙이 없을 때도 기존 영상 처리를 중단하지 않는다.

Windows에서는 `CreateProcess()`와 익명 파이프를 사용한다. Linux와
Raspberry Pi에서는 `fork()`, `execvp()`, `poll()`을 사용한다.

#### F. 최신 결과와 stale 처리

수신한 XML이 오래되면 예전 박스가 계속 남지 않도록 한다.

- `FRESH_MS` 이내의 결과만 화면에 표시한다.
- 그보다 오래된 박스는 빈 사람 목록으로 반환한다.
- `streamConnected`, `bytesReceived`, `ageMs`, `status`로 현재 수신
  상태를 확인할 수 있다.

#### G. 자동 재연결

FFmpeg가 종료되거나 카메라 연결이 끊기면 수신 스레드가 일정 시간 후
다시 연결한다. 영상 스트림 재연결과는 독립적으로 동작한다.

현재 종료 시 렉처럼 보이는 이유도 이 재연결 대기와 관련 있다.
`q`가 인식되지 않는 것이 아니라 `stop()`이 채널별 대기 스레드를
순차적으로 `join()`하기 때문이다.

### 2.3 `CheckWiseAiMetadata.ps1`

실제 프로그램에 반드시 필요한 파일은 아니며 카메라 설정을 확인하는
Windows PowerShell 보조 도구다.

기능:

1. `ffmpeg`, `ffprobe`가 PATH에 있는지 확인한다.
2. 카메라 비밀번호를 `SecureString`으로 입력받는다.
3. 4개 채널의 RTSP 데이터 트랙을 각각 확인한다.
4. 필요하면 일정 시간의 원본 XML을 파일로 저장한다.

기본 점검:

```powershell
.\CheckWiseAiMetadata.ps1 -CameraIp 172.20.35.186
```

10초 XML 저장:

```powershell
.\CheckWiseAiMetadata.ps1 -CameraIp 172.20.35.186 -CaptureSeconds 10
```

생성되는 `wiseai_ch*_sample.xml`은 디버깅용이다. 개인정보나 객체
위치가 들어갈 수 있으므로 Git에는 올리지 않는 것이 좋다.

### 2.4 `README_WISEAI_PERSON.md`

WiseAI 기능만 빠르게 설정하고 시험하려는 사람을 위한 간단한 안내서다.

포함 내용:

- 카메라에서 채널별 `Object detection → Person` 활성화
- 4개 RTSP 프로필 경로
- Windows FFmpeg 점검 방법
- 콘솔 좌표 형식
- Raspberry Pi FFmpeg 설치 명령

`RP_FIRE_FILE_GUIDE_KO.md`가 개발자용 상세 설명서라면,
`README_WISEAI_PERSON.md`는 설치·실행 담당자용 요약 문서다.

---

## 3. 이번 작업에서 수정한 기존 파일

### 3.1 `AppConfig.h`

프로젝트의 주요 설정값을 한곳에 모은 파일이다.

#### 입력 설정

- `USE_VIDEO_FILE`  
  `0`이면 RTSP 카메라, `1`이면 동영상 파일을 사용한다.

- `VIDEO_FILE_PATH`  
  동영상 시험 모드에서 사용할 파일 경로다.

- `RTSP_USERNAME`, `RTSP_PASSWORD`  
  카메라 인증에 사용한다. 공개 GitHub 저장소에 올릴 때 실제
  비밀번호가 포함되지 않도록 반드시 확인해야 한다.

- `RTSP_PROFILE_SUFFIX`  
  채널 번호 뒤에 붙는 `/profile10/media.smp` 경로다.

#### 화염 설정

`flame_config` namespace에 포함된다.

- 분석 해상도
- 검출 주기
- OpenCV 스레드 수
- 배경 모델 초기화 프레임
- 색상 임계값
- contour 최소 크기
- 화염 추적 확정 조건

#### 연기 설정

`smoke_config` namespace에 포함된다.

- NCNN 입력 크기 `416×256`
- 최대 채널 수 `4`
- 채널당 추론 간격
- NCNN CPU 스레드 수
- smoke 클래스 번호
- confidence와 NMS 임계값
- 연속 검출 횟수
- 모델 `.param`, `.bin` 상대 경로

#### 사람 메타데이터 설정

`person_metadata_config` namespace를 이번 작업에서 추가했다.

- `ENABLED`  
  WiseAI 메타데이터 기능 사용 여부다.

- `FFMPEG_EXECUTABLE`  
  실행할 FFmpeg 명령 이름이다. 기본값은 `ffmpeg`다.

- `STREAM_MAP`  
  RTSP의 첫 번째 data 트랙을 선택한다.

- `SOCKET_TIMEOUT_US`  
  FFmpeg RTSP 소켓 타임아웃이다.

- `RECONNECT_MS`  
  연결 실패 후 재시도 간격이다.

- `FRESH_MS`  
  사람 박스가 최신이라고 판단하는 시간이다.

- `MIN_CONFIDENCE`  
  이 값보다 낮은 사람 결과는 버린다.

- `BOX_PADDING_*`  
  표시 박스 여백이다.

- `REPORT_INTERVAL_MS`  
  콘솔 좌표 출력 간격이다.

### 3.2 `DetectionTypes.h`

화염, 연기, 사람 감지 결과를 여러 파일이 공통으로 주고받기 위한 자료형
모음이다.

이번에 추가한 형식:

```cpp
struct PersonDetection
{
    cv::Rect box;
    double confidence;
    std::string objectId;
};
```

- `box`: 원본 영상 픽셀 좌표
- `confidence`: 카메라 WiseAI 신뢰도
- `objectId`: 카메라가 부여한 추적 객체 ID

```cpp
struct PersonMetadataFrame
{
    std::vector<PersonDetection> persons;
    bool receiverRunning;
    bool streamConnected;
    double ageMs;
    std::uint64_t bytesReceived;
    std::string status;
};
```

- `persons`: 현재 사람 목록
- `receiverRunning`: 수신 스레드가 실행 중인지 표시
- `streamConnected`: 실제 XML 데이터가 들어오는지 표시
- `ageMs`: 마지막 XML 프레임의 나이
- `bytesReceived`: 누적 메타데이터 수신량
- `status`: 연결, 수신, 오류 상태 설명

기존 형식:

- `DetectionBox`: 화염 또는 연기 박스 하나
- `DetectionResult`: OpenCV 화염 검출 결과
- `SmokeDetectionResult`: NCNN 연기 검출 결과
- `FireDebugImages`: 화염 디버그 마스크

### 3.3 `ConsoleFireApplication.cpp`

프로그램 전체를 조립하고 실행하는 중심 파일이다.

#### `InputSelection`

각 입력 채널의 URL, 화면 이름, 입력 종류, 동영상 반복 여부를 저장한다.

#### `ChannelContext`

채널 하나가 사용하는 모든 상태를 묶는다.

```text
CameraStream
FireDetectionRuntime
PersonMetadataReceiver
최신 영상 프레임
최신 화염 결과
최신 연기 결과
최신 사람 결과
FPS 및 콘솔 출력 상태
```

연기 모델은 `ChannelContext` 안에 없다. 연기 모델 하나를 4채널이
공유하기 때문이다.

#### `makeRtspSource()`

입력한 카메라 IP와 채널 번호를 조합해 RTSP URL을 만든다.

#### `selectInputs()`

- 카메라 모드에서는 IP 하나만 입력받는다.
- 해당 IP를 사용해 CH1~CH4 URL을 자동 생성한다.
- 동영상 모드에서는 한 개의 영상 파일을 선택한다.

#### `reportStateChange()`

화재와 연기 상태가 바뀔 때 콘솔에 다음 정보를 출력한다.

```text
fire
smoke
smokeScore
smokeFrame
```

나중에 Raspberry Pi 서버가 Qt로 전달해야 하는 기본 값이다.

#### `reportPersonBoxes()`

이번에 추가했다. 사람 메타데이터 상태와 좌표를 다음 형식으로 출력한다.

```text
CH1 | personMeta=1 | persons=1 |
boxes=[{x:120,y:42,w:96,h:244,score:0.91,id:17}]
```

사람이 움직이는 동안에는 설정된 간격마다 좌표를 갱신한다. 사람이 없고
상태 변화도 없으면 불필요한 콘솔 출력을 줄인다.

#### `run()`

실제 실행 순서:

1. 입력 4채널 생성
2. 채널별 `CameraStream` 시작
3. 채널별 `PersonMetadataReceiver` 시작
4. NCNN 연기 모델 한 번 로드
5. 각 채널 최신 프레임 수집
6. 화염 런타임에 프레임 전달
7. 공유 연기 런타임에 채널 번호와 프레임 전달
8. 최신 사람 메타데이터 조회
9. 4분할 화면 작성
10. `q` 또는 `ESC` 입력 시 모든 런타임 종료

### 3.4 `FireView.h`, `FireView.cpp`

검출 결과를 화면에 그린다.

`FireViewChannel`에는 다음 값이 들어간다.

```text
원본 프레임
화염 snapshot
연기 snapshot
사람 metadata
표시 FPS
채널 이름
```

박스 색상:

- 화염: 빨간색
- 연기: 청록색
- 사람: 초록색

화면 위 정보:

```text
CH 번호
NORMAL / FIRE / SMOKE 상태
FPS
화염 처리 시간
연기 처리 시간
연기 score와 hits
WiseAI 연결 상태와 사람 수
```

`showGrid()`는 항상 타일 네 개를 만들고 2×2로 합친다. 신호가 없는
채널은 `NO SIGNAL`로 표시한다.

`q` 또는 `ESC`가 들어오면 `false`를 반환하고 메인 루프가 종료 절차로
이동한다.

### 3.5 `CMakeLists.txt`

Windows 또는 Raspberry Pi에서 CMake로 빌드하기 위한 설정이다.

역할:

- C++17 설정
- OpenCV 검색 및 링크
- NCNN 검색 및 링크
- `PersonMetadataReceiver.cpp`를 빌드 대상에 포함
- 빌드 후 `models` 디렉터리를 실행 파일 옆으로 복사
- Windows는 `/O2`, Linux는 `-O3` 최적화 적용

프로젝트 경로에 `&`가 포함되어도 모델 복사 명령이 깨지지 않도록
`VERBATIM` 옵션이 포함되어 있다.

### 3.6 `RP_Fire.vcxproj`

Visual Studio C++ 프로젝트 본체다.

이번 작업에서는 다음 파일을 Visual Studio 빌드 목록에 추가했다.

```text
PersonMetadataReceiver.cpp
PersonMetadataReceiver.h
```

이 등록이 없으면 파일이 폴더에 존재하더라도 Visual Studio 빌드 시
컴파일되지 않는다.

### 3.7 `RP_Fire.vcxproj.filters`

Visual Studio 솔루션 탐색기에서 파일을 `소스 파일`, `헤더 파일` 아래에
보여주기 위한 분류 파일이다. 실행 결과에는 영향을 주지 않지만 프로젝트
관리 편의를 위해 필요하다.

---

## 4. 기존 화염 감지 파일

### 4.1 `main.cpp`

프로그램 시작점이다.

1. OpenCV 내부 스레드 수를 설정한다.
2. `ConsoleFireApplication` 객체를 만든다.
3. `run()`을 호출한다.

화염 검출 스레드가 채널별로 존재하더라도 OpenCV 내부 병렬 스레드를
제한해 4채널에서 CPU 스레드가 과도하게 늘어나는 것을 막는다.

### 4.2 `CameraStream.h`, `CameraStream.cpp`

RTSP 또는 동영상 파일을 읽는 비동기 입력 클래스다.

주요 동작:

- 채널마다 영상 읽기 스레드 하나 사용
- 항상 가장 최신 프레임만 보관
- 처리 속도가 카메라 FPS보다 느려도 오래된 프레임을 쌓지 않음
- RTSP 연결 실패 시 자동 재연결
- 동영상 파일 반복 재생 지원

RTSP 타임아웃:

```text
연결: 5000ms
프레임 읽기: 2000ms
```

현재 `stop()`은 `running_`을 끈 후 영상 스레드 `join()`을 기다린다.
스레드가 `cap_.read()` 중이면 읽기 타임아웃까지 종료가 늦어질 수 있다.

### 4.3 `FlameDetector.h`, `FlameDetector.cpp`

OpenCV 기반 화염 검출기의 핵심이다.

YOLO 모델을 사용하지 않고 다음 특징을 결합한다.

- 빨강·주황 계열 색상
- 밝기와 주변 밝기 차이
- MOG2 전경 움직임
- contour 크기와 형태
- 흰색 화염 중심부
- 피부색 유사도
- texture entropy와 energy
- 프레임 간 마스크 변화
- 시간에 따른 동일 후보 추적

`detect()`가 프레임 한 장을 분석해 `DetectionResult`를 반환한다.

내부 `Track`은 후보마다 다음 값을 보관한다.

```text
id
box
hits
misses
strongHits
score
confirmed
areaHistory
```

짧게 나타난 따뜻한 색 물체를 바로 화염으로 확정하지 않고, 동일 위치의
후보가 반복되는지 확인한다.

### 4.4 `FireDetectionRuntime.h`, `FireDetectionRuntime.cpp`

`FlameDetector`를 메인 화면 루프와 분리해 비동기로 실행한다.

역할:

- 최신 프레임만 작업 큐에 유지
- 화염 검출 스레드 실행
- 처리 시간 측정
- 오래된 결과와 박스 제거
- `FireAlarmController`에 결과 전달
- 화면 및 Qt 서버가 읽기 쉬운 `FireRuntimeSnapshot` 제공

채널마다 `FireDetectionRuntime` 객체가 하나씩 생성된다.

### 4.5 `FireAlarmController.h`, `FireAlarmController.cpp`

`FlameDetector`가 확정한 결과를 최종 화재 상태로 바꾸는 마지막
hysteresis 단계다.

- 일반 화염: 짧은 120ms 확인
- 애매한 따뜻한 물체: 더 긴 900ms와 여러 결과 요구
- 검출이 잠깐 끊겼을 때 즉시 상태가 깜박이지 않도록 증거값 감쇠

`FireAlarmStatus`에는 최종 알람, 진행 시간, 요구 시간, 원시 결과 수가
들어 있다.

### 4.6 `FireCandidateTracker.h`, `FireCandidateTracker.cpp`

별도로 존재하는 이전 후보 추적 구현이다. 현재 실제 화염 추적은
`FlameDetector` 내부 `Track` 구조가 담당하며, 이 파일은 현재 CMake와
Visual Studio 실행 빌드 목록에 포함되지 않는다.

따라서 현재 실행에 필수인 파일은 아니지만 이전 구현 비교나 향후 분리
리팩터링을 위해 남아 있다. 사용하지 않을 파일을 정리할 때는 실제 참조
여부를 다시 확인한 뒤 처리해야 한다.

---

## 5. 기존 연기 감지 파일

### 5.1 `SmokeDetector.h`, `SmokeDetector.cpp`

NCNN 형식의 YOLO 연기 모델을 직접 실행한다.

주요 함수:

- `load(paramPath, binPath)`  
  `.param`과 `.bin`을 읽어 NCNN 네트워크를 준비한다.

- `detect(inputFrame)`  
  프레임을 416×256 입력에 맞게 letterbox 처리하고 추론한다.

- `isReady()`  
  모델이 정상적으로 로드되었는지 반환한다.

- `lastError()`  
  로드 또는 추론 오류 내용을 반환한다.

출력 후처리:

- smoke 클래스만 사용
- confidence 임계값 적용
- NMS로 중복 박스 제거
- 좌표를 원본 640×360 영상으로 복원

### 5.2 `SmokeDetectionRuntime.h`, `SmokeDetectionRuntime.cpp`

연기 모델 하나를 4채널이 순차적으로 공유하게 한다.

구조:

```text
CH1 최신 프레임 ┐
CH2 최신 프레임 ├─> 공유 작업 스레드 ─> NCNN 모델 1개
CH3 최신 프레임 ┤
CH4 최신 프레임 ┘
```

각 채널은 최신 대기 프레임 하나만 가진다. 오래된 대기 프레임은 새
프레임으로 교체되므로 지연이 계속 누적되지 않는다.

추론 결과에는 다음 정보가 있다.

```text
smokeDetected
smokeScore
positiveHits
consecutiveMisses
averageDetectMs
resultAgeMs
modelError
```

### 5.3 NCNN 모델 디렉터리

현재 설정이 사용하는 경로:

```text
models/
└─ smoke_yolov8n_round4b_field_calibrated_20260827_416x256_ncnn_model/
   ├─ model.ncnn.param
   ├─ model.ncnn.bin
   └─ metadata.yaml
```

#### `model.ncnn.param`

- 약 16KB
- 신경망 레이어 구조
- 입력·출력 blob 이름
- 레이어 연결 정보

#### `model.ncnn.bin`

- 약 12MB
- 학습된 가중치
- 실제 연기 특징 정보

`.param`만 있거나 `.bin`만 있으면 연기 모델을 실행할 수 없다. 두 파일이
모두 필요하다.

#### `metadata.yaml`

모델 출처, 입력 크기, 클래스 이름 등의 부가 정보다. 현재 C++ 추론은
`.param`, `.bin`을 직접 사용하므로 핵심 실행에는 두 NCNN 파일이 가장
중요하다. 모델 관리와 확인을 위해 `metadata.yaml`도 함께 보관하는 것을
권장한다.

라즈베리파이에는 `best.pt`, PyTorch, Ultralytics YOLO 패키지가 필요하지
않다. NCNN 라이브러리와 위 모델 파일만 필요하다.

---

## 6. 라이브러리 및 실행 보조 파일

### 6.1 `third_party/ncnn`

Windows Visual Studio 빌드에 사용하는 NCNN 헤더, 라이브러리, DLL이
들어 있다.

```text
include/ncnn/*.h
lib/ncnn.lib
bin/ncnn.dll
```

Raspberry Pi에서는 Windows용 `.lib`, `.dll`을 사용할 수 없다. Pi에서
ARM용 NCNN을 설치하거나 ARM용으로 직접 빌드해야 한다.

### 6.2 Windows OpenCV DLL

Visual Studio Release 실행 파일 옆에는 일반적으로 다음 DLL이 필요하다.

```text
opencv_world4120.dll
opencv_videoio_ffmpeg4120_64.dll
ncnn.dll
```

이는 Windows 실행용이다. Raspberry Pi에서는 Linux ARM용 OpenCV와
NCNN 공유 라이브러리를 사용한다.

### 6.3 `README_CPU_OPTIMIZATION.md`

라즈베리파이 CPU 사용량을 줄이기 위한 기존 최적화 설명서다.

### 6.4 `README_SMOKE_NCNN.md`

NCNN 연기 모델 설정과 빌드에 관한 기존 설명서다.

---

## 7. GitHub에 올려야 하는 파일

### 반드시 포함

```text
*.cpp
*.h
CMakeLists.txt
RP_Fire.vcxproj
RP_Fire.vcxproj.filters
models/smoke_yolov8n_round4b_field_calibrated_20260827_416x256_ncnn_model/model.ncnn.param
models/smoke_yolov8n_round4b_field_calibrated_20260827_416x256_ncnn_model/model.ncnn.bin
models/smoke_yolov8n_round4b_field_calibrated_20260827_416x256_ncnn_model/metadata.yaml
```

### 함께 올리는 것을 권장

```text
README_WISEAI_PERSON.md
RP_FIRE_FILE_GUIDE_KO.md
CheckWiseAiMetadata.ps1
README_CPU_OPTIMIZATION.md
README_SMOKE_NCNN.md
```

`CheckWiseAiMetadata.ps1`은 실제 프로그램 실행에는 필수가 아니지만,
다른 PC나 현장에서 카메라 메타데이터 문제를 확인할 때 유용하다.

### 보통 Git에서 제외

```text
x64/
Debug/
Release/
*.obj
*.pdb
*.ilk
*.iobj
*.ipdb
*.tlog
*.user
wiseai_ch*_sample.xml
person_metadata*.log
```

빌드 결과물은 운영 배포 패키지나 GitHub Release로 따로 제공할 수 있지만
소스 저장소의 일반 커밋에는 넣지 않는 것이 좋다.

### 보안 확인

공개 저장소에 push하기 전 다음 항목을 확인한다.

```text
RTSP 사용자 이름
RTSP 비밀번호
카메라 IP
메타데이터 원본 XML
카메라 화면 캡처
```

특히 `AppConfig.h`에 실제 비밀번호가 들어 있으면 공개 GitHub에 그대로
올리면 안 된다.

---

## 8. Raspberry Pi 배포 시 필요한 항목

필수:

```text
RP_Fire 소스 또는 ARM으로 빌드한 실행 파일
ARM용 OpenCV
ARM용 NCNN
FFmpeg
model.ncnn.param
model.ncnn.bin
```

FFmpeg 설치:

```bash
sudo apt update
sudo apt install -y ffmpeg
```

실행 파일 기준 모델 배치:

```text
배포 폴더/
├─ fire_detection
└─ models/
   └─ smoke_yolov8n_round4b_field_calibrated_20260827_416x256_ncnn_model/
      ├─ model.ncnn.param
      ├─ model.ncnn.bin
      └─ metadata.yaml
```

사람 감지는 카메라 WiseAI가 수행하므로 Raspberry Pi에 사람 검출 YOLO
모델을 추가로 올리지 않는다.

---

## 9. 화면과 콘솔 상태 해석

### 화면

```text
WiseAI ON  | 메타데이터 XML을 실제 수신 중
WiseAI OFF | 메타데이터가 없거나 연결되지 않음
persons N  | 현재 최신 사람 박스 개수
```

### 콘솔

```text
personMeta=1
```

메타데이터 데이터 트랙에서 XML이 들어오고 있다는 뜻이다.

```text
persons=0
```

연결은 정상이나 현재 사람이 없을 수 있다.

```text
personMeta=0
```

다음 가능성을 확인한다.

1. 카메라 접근 불가
2. 채널별 Object detection이 꺼져 있음
3. Person 클래스가 선택되지 않음
4. RTSP 프로필에 metadata data 트랙이 없음
5. FFmpeg가 PATH에 없음
6. 사용자 권한 또는 비밀번호 오류

---

## 10. 종료 시 지연이 발생하는 현재 이유

`q` 또는 `ESC` 입력 자체는 즉시 인식된다. 이후 다음 정리 작업을
기다리기 때문에 창이 잠시 멈춘 것처럼 보일 수 있다.

1. 공유 연기 추론 스레드 종료
2. CH1 WiseAI 수신 스레드 종료
3. CH1 화염 스레드 종료
4. CH1 RTSP 영상 스레드 종료
5. 같은 작업을 CH2~CH4에 순차 반복

WiseAI 연결이 실패하여 재연결 대기 중이면 채널마다 최대
`RECONNECT_MS`만큼 기다릴 수 있다. RTSP `cap_.read()` 중이면
`CAP_PROP_READ_TIMEOUT_MSEC`까지 기다릴 수 있다.

향후 즉시 종료 구조로 수정하려면:

- `sleep_for()` 대신 `condition_variable::wait_for()` 사용
- 종료 시 4채널 모두에 먼저 중단 신호 전달
- 그 다음 모든 스레드 `join()`
- 필요한 경우 `VideoCapture` 읽기 중단 구조 개선

이 문제는 감지 성능 문제가 아니라 종료 정리 순서와 블로킹 I/O 문제다.

---

## 11. 최종 현장 점검 순서

1. 카메라 4개 센서에서 `Object detection`을 켠다.
2. 모든 채널에서 `Person`을 선택한다.
3. `CheckWiseAiMetadata.ps1`로 data 트랙을 확인한다.
4. Visual Studio Release 또는 Raspberry Pi CMake 빌드를 실행한다.
5. 화면의 `WiseAI ON`을 확인한다.
6. 사람 앞에서 초록색 박스가 나타나는지 확인한다.
7. 콘솔의 `x, y, w, h`가 640×360 범위인지 확인한다.
8. 화염과 연기 검출이 이전과 동일하게 동작하는지 확인한다.
9. 카메라 네트워크를 잠깐 끊었다가 자동 재연결되는지 확인한다.
10. 종료 지연 시간을 확인한다.
