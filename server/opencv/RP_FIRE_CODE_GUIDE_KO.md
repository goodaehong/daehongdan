# RP_Fire 코드 상세 해설서

> 보관 문서: 이 문서의 `main`·`CameraStream`·`ConsoleFireApplication`·
> `FireView`는 현재 `daehongdan` 서버 실행 경로에서 사용하지 않는 이전
> 독립 하네스다. 실제 배포 코어는 `server/CMakeLists.txt`의 `server_main`에서
> 빌드하며, 이 문서는 개발 과정 설명을 위해 유지한다.

이 문서는 `RP_Fire`의 실제 코드를 위에서 아래로 읽을 수 있도록 설명한다.
전체 파일의 용도와 배포 파일 목록은
[`RP_FIRE_FILE_GUIDE_KO.md`](RP_FIRE_FILE_GUIDE_KO.md)를 참고한다.

설명은 코드를 그대로 복사하는 대신 중요한 코드 블록을 뽑아서 다음
세 가지를 밝힌다.

1. 이 코드가 언제 실행되는가
2. 각 변수와 조건문이 무엇을 의미하는가
3. 다음 코드로 어떤 값이 전달되는가

---

## 1. 먼저 알아야 하는 C++ 구조

### 1.1 `cv::Mat`

OpenCV 영상 한 장을 나타낸다.

```cpp
cv::Mat frame;
```

`frame`에는 픽셀 데이터, 가로·세로 크기, 채널 수가 들어 있다. 일반
카메라 컬러 영상은 BGR 3채널이다.

### 1.2 `std::atomic<bool>`

여러 스레드가 동시에 읽고 써도 데이터 경합이 나지 않는 상태값이다.

```cpp
std::atomic<bool> running_{ false };
```

영상 스레드는 `running_ == true`인 동안 반복하고, 메인 스레드가
`false`로 바꾸면 종료를 준비한다.

### 1.3 `std::mutex`와 `lock_guard`

두 스레드가 같은 프레임이나 결과를 동시에 수정하지 못하게 잠근다.

```cpp
lock_guard<mutex> lock(frameMutex_);
latestFrame_ = frame;
```

중괄호를 빠져나가면 `lock`이 자동으로 mutex를 해제한다.

### 1.4 `condition_variable`

작업이 없을 때 스레드가 CPU를 사용하며 계속 확인하지 않고 잠들게 한다.

```cpp
condition_.wait(lock, [&] {
    return !running_.load() || hasPendingFrame();
});
```

종료 신호가 왔거나 처리할 프레임이 생겼을 때만 깨어난다.

### 1.5 Snapshot

검출 스레드 내부 상태를 화면이나 서버가 안전하게 읽을 수 있도록 복사한
한 시점의 결과다.

```text
FireRuntimeSnapshot
SmokeRuntimeSnapshot
PersonMetadataFrame
```

화면 코드는 검출기 내부 변수에 직접 접근하지 않고 snapshot만 사용한다.

### 1.6 PImpl

헤더에는 `class Impl;`만 선언하고 복잡한 구현은 `.cpp`에 숨기는 구조다.

```cpp
class SmokeDetector
{
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
```

NCNN, Windows API, Linux API가 바뀌어도 다른 파일의 헤더 의존성을 작게
유지할 수 있다.

---

## 2. `main.cpp`

전체 코드는 다음 세 단계다.

```cpp
int main()
{
    cv::setNumThreads(flame_config::OPENCV_NUM_THREADS);

    ConsoleFireApplication application;
    return application.run();
}
```

### `cv::setNumThreads(...)`

OpenCV 함수 하나가 내부적으로 너무 많은 CPU 스레드를 만들지 않도록
제한한다. 현재 화염 채널 작업은 별도 런타임으로 나뉘어 있으므로 OpenCV
내부 병렬화까지 크게 허용하면 Raspberry Pi 4의 CPU 코어가 과도하게
경쟁한다.

### `ConsoleFireApplication application`

카메라, 화염, 연기, WiseAI, 화면을 조립하는 최상위 객체를 생성한다.

### `return application.run()`

실제 프로그램을 실행하고 종료 코드를 운영체제에 반환한다.

---

## 3. `AppConfig.h`

코드에 숫자와 경로를 직접 흩어놓지 않고 의미 있는 이름으로 관리한다.

### 3.1 카메라 모드

```cpp
#ifndef USE_VIDEO_FILE
#define USE_VIDEO_FILE 0
#endif
```

- `0`: RTSP 카메라
- `1`: 동영상 파일

`#ifndef`를 사용했기 때문에 빌드 명령에서 값을 따로 정의하면 소스를
수정하지 않고 덮어쓸 수 있다.

### 3.2 화염 분석 주기

```cpp
constexpr int DETECTION_INTERVAL_MS = 167;
```

각 채널 화염 분석을 약 6FPS로 제한한다.

```text
1000ms / 167ms ≈ 6회/초
```

카메라 영상 표시 자체는 더 빠르게 갱신할 수 있지만 무거운 화염 분석은
모든 프레임에 실행하지 않는다.

### 3.3 연기 입력

```cpp
constexpr int INPUT_WIDTH = 640;
constexpr int INPUT_HEIGHT = 384;
```

실제 영상은 640×360이다. YOLO stride 조건을 맞추기 위해 위아래에
letterbox 여백을 추가하여 640×384 텐서로 만든다. 원본 영상 비율을
억지로 늘리지 않는다.

### 3.4 공유 연기 추론

```cpp
constexpr int INFERENCE_INTERVAL_MS = 1000;
```

각 채널은 1초마다 가장 최신 프레임을 공유 워커에 제출한다. 워커는 채널을
순서대로 처리하며 실제 처리 속도는 Raspberry Pi의 NCNN 추론 시간에 좌우된다.

### 3.5 WiseAI 설정

```cpp
constexpr const char* STREAM_MAP = "0:d:0?";
```

- `0`: 첫 번째 RTSP 입력
- `d`: data 형식 스트림
- `0`: 첫 번째 data 스트림
- `?`: 해당 스트림이 없어도 FFmpeg 전체 실행을 강제 실패시키지 않음

```cpp
constexpr int FRESH_MS = 1500;
```

마지막 XML 결과가 1.5초보다 오래되면 과거 사람 박스를 지운다.

---

## 4. `DetectionTypes.h`

### 4.1 사람 한 명

```cpp
struct PersonDetection
{
    cv::Rect box;
    double confidence = 0.0;
    std::string objectId;
};
```

`cv::Rect`의 네 값:

```text
box.x      왼쪽 위 X
box.y      왼쪽 위 Y
box.width  너비
box.height 높이
```

### 4.2 사람 메타데이터 한 프레임

```cpp
struct PersonMetadataFrame
{
    std::vector<PersonDetection> persons;
    bool receiverRunning = false;
    bool streamConnected = false;
    double ageMs = infinity;
    std::uint64_t bytesReceived = 0;
    std::string status;
};
```

`receiverRunning`과 `streamConnected`는 다르다.

- `receiverRunning=true`, `streamConnected=false`  
  수신 스레드는 살아 있지만 카메라 XML은 아직 들어오지 않음

- 둘 다 `true`  
  실제 XML 바이트를 수신 중

### 4.3 화염과 연기 박스

```cpp
struct DetectionBox
{
    cv::Rect box;
    std::string label;
    DetectionType type;
    double score;
    ...
};
```

공통 좌표와 점수 외에 화염 후보가 피부색인지, 강한 화염 증거가 있는지,
추가 확인이 필요한지 등의 판단값도 포함한다.

---

## 5. `ConsoleFireApplication.cpp`

## 5.1 `InputSelection`

```cpp
struct InputSelection
{
    string source;
    string displayName;
    StreamSourceType type = StreamSourceType::RtspCamera;
    bool loop = false;
};
```

카메라 또는 동영상 입력 하나를 설명한다.

## 5.2 `ChannelContext`

```cpp
struct ChannelContext
{
    unique_ptr<CameraStream> camera;
    unique_ptr<FireDetectionRuntime> fireRuntime;
    unique_ptr<PersonMetadataReceiver> personReceiver;

    Mat latestDisplayFrame;
    FireRuntimeSnapshot latestFireSnapshot;
    SmokeRuntimeSnapshot latestSmokeSnapshot;
    PersonMetadataFrame latestPersonMetadata;
    ...
};
```

채널마다 카메라, 화염 런타임, WiseAI 수신기를 별도로 가진다. 연기
런타임은 여기에 없는 것이 중요하다. 연기 모델은 프로그램 전체에서
하나만 생성한다.

`unique_ptr`를 사용하므로 `ChannelContext`가 파괴될 때 객체도 자동으로
정리된다.

## 5.3 RTSP URL 생성

```cpp
return string("rtsp://") + RTSP_USERNAME + ':' + RTSP_PASSWORD + '@' +
    cameraIp + ":554/" + to_string(channelIndex) + RTSP_PROFILE_SUFFIX;
```

입력된 IP 하나에 채널 번호 `0~3`을 결합한다.

예를 들어 `channelIndex == 2`이면 세 번째 센서 URL이 된다.

## 5.4 4채널 생성

```cpp
for (int index = 0; index < smoke_config::MAX_CHANNELS; ++index)
{
    InputSelection input;
    input.source = makeRtspSource(cameraIp, index);
    input.displayName = "CH" + to_string(index + 1);
    inputs.push_back(std::move(input));
}
```

사용자는 IP를 한 번만 입력한다. 루프가 `/0`, `/1`, `/2`, `/3`을
자동으로 만든다. 화면 이름은 사람이 보기 편하게 `CH1~CH4`로 표시한다.

## 5.5 사람 좌표 콘솔 출력

```cpp
cout << "{x:" << person.box.x
    << ",y:" << person.box.y
    << ",w:" << person.box.width
    << ",h:" << person.box.height
    << ",score:" << person.confidence;
```

이 부분은 나중에 Qt 서버 전송 구조로 옮길 수 있는 실제 좌표값이다.
화면에서 확대된 좌표가 아니라 원본 채널 해상도 기준 좌표다.

## 5.6 카메라와 WiseAI 시작

```cpp
if (!channel->camera->start())
{
    return -1;
}

if (person_metadata_config::ENABLED &&
    channel->input.type == StreamSourceType::RtspCamera)
{
    channel->personReceiver->start(channel->input.source);
}
```

영상 스레드를 먼저 시작한다. WiseAI는 RTSP 카메라 모드에서만 실행한다.
동영상 파일에는 카메라 XML data 트랙이 없으므로 시작하지 않는다.

WiseAI 시작 실패는 전체 프로그램 종료 조건이 아니다. 사람 박스만
비활성화하고 화염·연기 처리는 유지한다.

## 5.7 연기 모델 한 번 로드

```cpp
SmokeDetectionRuntime smokeRuntime(
    channels.size(),
    smoke_config::MODEL_PARAM_PATH,
    smoke_config::MODEL_BIN_PATH);
```

이 코드는 채널 루프 밖에 있다. 따라서 4채널이어도 NCNN 모델은 한 번만
메모리에 올라간다.

## 5.8 메인 프레임 처리

```cpp
channel.fireRuntime->submitFrame(frame, channel.lastFrameId, now);
smokeRuntime.submitFrame(channel.index, frame, channel.lastFrameId, now);
```

동일한 원본 프레임을 다음 두 처리기에 전달한다.

- 채널 전용 화염 런타임
- 전체 공유 연기 런타임

두 함수 모두 오래된 프레임 큐를 쌓지 않는다.

```cpp
const FireRuntimeSnapshot fireSnapshot = channel.fireRuntime->poll(now);
const SmokeRuntimeSnapshot smokeSnapshot = smokeRuntime.poll(channel.index, now);
const PersonMetadataFrame personMetadata =
    channel.personReceiver->snapshot(frame.size());
```

제출한 직후 반드시 그 프레임 결과가 나오는 것은 아니다. 비동기 스레드가
완료한 가장 최신 결과를 `poll()`과 `snapshot()`으로 읽는다.

## 5.9 화면 데이터 구성

```cpp
FireViewChannel viewChannel;
viewChannel.frame = channel->latestDisplayFrame;
viewChannel.fire = channel->latestFireSnapshot;
viewChannel.smoke = channel->latestSmokeSnapshot;
viewChannel.person = channel->latestPersonMetadata;
```

한 채널에 필요한 모든 표시 데이터를 한 구조체로 묶어 `showGrid()`에
전달한다.

## 5.10 종료

```cpp
smokeRuntime.stop();
for (unique_ptr<ChannelContext>& channel : channels)
{
    channel->personReceiver->stop();
    channel->fireRuntime->stop();
    channel->camera->stop();
}
```

현재는 채널별로 순차 종료한다. WiseAI 재연결 `sleep_for()`나
`VideoCapture::read()`가 블로킹 중이면 `join()`이 이를 기다리므로 `q`
입력 후 바로 창이 닫히지 않을 수 있다.

---

## 6. `CameraStream.cpp`

## 6.1 시작

```cpp
bool expected = false;
if (!running_.compare_exchange_strong(expected, true)) return false;
readerThread_ = thread(&CameraStream::readLoop, this);
```

이미 실행 중이면 두 번째 스레드를 만들지 않는다. 처음 실행할 때만
`running_`을 `true`로 변경하고 `readLoop()` 스레드를 만든다.

## 6.2 최신 프레임만 반환

```cpp
if (!hasFrame_.load() || latestFrame_.empty() ||
    latestFrameId_ == lastFrameId)
    return false;

outFrame = latestFrame_;
lastFrameId = latestFrameId_;
```

호출자가 이미 읽은 `frameId`와 같으면 같은 프레임을 중복 처리하지 않는다.
프레임 큐가 아니라 최신 프레임 하나만 보관하므로 실시간 지연 누적을
막는다.

## 6.3 RTSP 타임아웃

```cpp
const vector<int> openParams = {
    CAP_PROP_OPEN_TIMEOUT_MSEC, 5000,
    CAP_PROP_READ_TIMEOUT_MSEC, 2000
};
```

- 연결 시도는 최대 5초
- 연결 후 한 프레임 읽기는 최대 2초

## 6.4 자동 재연결

```cpp
if (!cap_.read(frame) || frame.empty())
{
    opened_ = false;
    cap_.release();
    this_thread::sleep_for(chrono::milliseconds(200));
    continue;
}
```

프레임 읽기에 실패하면 캡처 객체를 닫는다. 다음 반복에서
`!cap_.isOpened()`가 되어 `openSource()`를 다시 호출한다.

---

## 7. `FireDetectionRuntime.cpp`

## 7.1 채널 위상 분산

```cpp
return index * flame_config::DETECTION_INTERVAL_MS /
    CHANNEL_PHASE_COUNT;
```

4채널이 정확히 같은 순간에 화염 분석을 요청하지 않도록 시작 시점을
조금씩 나눈다.

## 7.2 전역 화염 실행 mutex

```cpp
std::mutex gFireDetectorExecutionMutex;
```

채널별 런타임 스레드는 네 개지만 실제 무거운 `detector_.detect()`는
다음 잠금 때문에 한 번에 하나만 실행된다.

```cpp
lock_guard<mutex> detectorLock(gFireDetectorExecutionMutex);
detection = detector_.detect(frame);
```

Raspberry Pi 4에서 네 채널이 동시에 OpenCV 연산을 시작해 CPU가 포화되는
것을 막는다.

## 7.3 제출 간격 제한

```cpp
if (sourceTime < nextAcceptedSubmitTime_)
    return;
```

167ms가 지나기 전에 들어온 프레임은 화염 분석 대상으로 복사하지 않는다.
화면 표시용 프레임 자체를 버린다는 뜻은 아니다.

## 7.4 Epoch

```cpp
pendingEpoch_ = streamEpoch_.load();
```

카메라가 재연결되면 epoch를 증가시킨다. 이전 연결에서 늦게 끝난 검출
결과의 epoch가 현재와 다르면 버린다.

## 7.5 결과 freshness

```cpp
snapshot.resultIsFresh =
    snapshot.hasResult &&
    snapshot.resultAgeMs <= snapshot.resultFreshLimitMs;
```

검출 결과가 너무 오래되면 현재 결과로 사용하지 않는다. 끊어진 영상의
마지막 화염 박스가 계속 남는 것을 방지한다.

---

## 8. `FlameDetector.cpp`

## 8.1 움직임 마스크

```cpp
mog2_->apply(gray, motion, flame_config::MOG2_LEARNING_RATE);
threshold(motion, motion, 200, 255, THRESH_BINARY);
```

MOG2가 배경과 다른 픽셀을 찾는다. threshold 이후 움직임 픽셀은 255,
배경은 0이 된다.

```cpp
absdiff(previousGray_, gray, diff);
bitwise_or(motion, diff, motion);
```

직전 프레임 차이도 합쳐 작은 불꽃 변화가 MOG2에서 빠지는 것을 보완한다.

## 8.2 불꽃 색상 조건

```cpp
if (r > ORIGINAL_RED_THRESHOLD &&
    r >= g && g > b &&
    saturation >= requiredSaturation)
{
    dst[x] = 255;
}
```

빨간 성분이 충분히 크고 `R ≥ G > B`인 움직이는 픽셀만 1차 화염색으로
선택한다.

## 8.3 피부 마스크

```cpp
inRange(ycrcb, ..., skinYCrCb);
inRange(hsv, ..., skinHSV);
bitwise_and(skinYCrCb, skinHSV, skin);
```

두 색공간에서 모두 피부 범위에 들어가는 픽셀을 찾는다. 손이나 얼굴을
불꽃으로 오인하는 것을 줄이기 위한 증거다.

## 8.4 흰색 중심부

```cpp
inRange(hsv, Scalar(0, 0, 220), Scalar(179, 75, 255), white);
dilate(colorMask, halo, kernel7_);
bitwise_and(white, halo, core);
```

밝고 채도가 낮은 픽셀 중 화염색 주변에 있는 부분만 흰색 화염 중심부로
취급한다.

## 8.5 후보 특징

`analyzeContour()`는 contour마다 다음 특징을 계산한다.

```text
colorCoverage       화염색 비율
motionCoverage      움직임 비율
redOrangeCoverage   빨강·주황 hue 비율
whiteCoreCoverage   흰 중심부 비율
skinCoverage        피부색 비율
vStd                밝기 변화
circularity         원형 유사도
solidity            볼록 영역 채움 정도
extent              사각 박스 채움 정도
textureEntropy      텍스처 복잡도
textureEnergy       텍스처 반복성
maskChange          시간에 따른 모양 변화
brightnessDelta     주변보다 얼마나 밝은지
```

## 8.6 점수 계산

```cpp
double score =
    0.20 * color +
    0.06 * motion +
    0.14 * redOrange +
    0.15 * whiteCore +
    ...
```

하나의 조건만으로 화염을 확정하지 않고 여러 특징을 0~1 점수로 결합한다.

피부 비율이 높고 독립적인 불꽃 구조가 없으면:

```cpp
if (f.skinCoverage >= 0.60 && !independentFlameStructure)
    return 0.0;
```

후보를 즉시 제거한다.

## 8.7 시간 추적

```cpp
if (track.hits >= flame_config::CONFIRM_HITS &&
    track.strongHits >= 2)
{
    track.confirmed = true;
}
```

현재 설정의 `CONFIRM_HITS`만큼 같은 위치의 후보가 반복되고, 그중 최소
두 번이 강한 점수여야 tracker가 확정한다.

## 8.8 최종 `detect()` 순서

```text
입력 축소
→ Gray/HSV 변환
→ 움직임 마스크
→ 화염색 마스크
→ 피부 마스크
→ 흰 중심 마스크
→ 후보 마스크 결합
→ contour 찾기
→ contour 특징과 점수
→ 동일 후보 추적
→ 원본 영상 좌표 복원
```

---

## 9. `FireAlarmController.cpp`

Tracker가 확정한 뒤 화면 알람이 너무 쉽게 깜박이지 않게 최종 상태를
관리한다.

## 9.1 일반과 애매한 후보

```cpp
const double requiredMs =
    extended ? AMBIGUOUS_CONFIRM_MS : FINAL_CONFIRM_MS;
```

- 일반 화염: 120ms
- 피부색과 유사한 애매한 후보: 900ms

## 9.2 동일 후보 확인

```cpp
return iou >= 0.10 ||
    centerDistance <= max(30.0, referenceSize * 0.90);
```

박스 IoU가 겹치거나 중심점이 충분히 가까우면 같은 불꽃으로 본다.

## 9.3 증거 누적과 감소

검출 중:

```cpp
fireEvidenceMs_ =
    min(activeConfirmMs_, fireEvidenceMs_ + intervalMs);
```

검출이 끊기면:

```cpp
fireEvidenceMs_ =
    max(0.0, fireEvidenceMs_ - intervalMs * decayRate);
```

한 프레임 누락으로 즉시 화재 상태를 끄지 않고 점진적으로 감소시킨다.

---

## 10. `SmokeDetector.cpp`

## 10.1 모델 경로 찾기

```cpp
if (is_regular_file(requestedPath))
    return requestedPath.string();

const path executablePath =
    executableDirectory() / requestedPath;
```

현재 작업 디렉터리와 실행 파일 옆을 모두 확인한다. Visual Studio에서
실행 위치가 달라도 `models` 디렉터리를 찾기 위한 처리다.

## 10.2 모델 로드

```cpp
net_.opt.use_vulkan_compute = false;
net_.opt.num_threads = smoke_config::NCNN_NUM_THREADS;
```

라즈베리파이 CPU 실행을 기준으로 Vulkan GPU를 끄고 NCNN CPU 스레드 수를
제한한다.

```cpp
net_.load_param(...);
net_.load_model(...);
```

`.param`은 네트워크 구조, `.bin`은 학습 가중치다. 둘 중 하나라도 실패하면
`ready_`는 false다.

## 10.3 Letterbox

```cpp
const float scale = min(
    INPUT_WIDTH / bgr.cols,
    INPUT_HEIGHT / bgr.rows);
```

가로와 세로 배율 중 작은 값을 사용해 원본 비율을 유지한다.

```cpp
copyMakeBorder(
    resized, letterboxed,
    top, bottom, left, right,
    BORDER_CONSTANT, Scalar(114, 114, 114));
```

남는 공간을 YOLO 기본 회색인 114로 채운다.

## 10.4 NCNN 입력

```cpp
ncnn::Mat input = ncnn::Mat::from_pixels(
    letterboxed.data,
    ncnn::Mat::PIXEL_BGR2RGB,
    INPUT_WIDTH,
    INPUT_HEIGHT);
```

OpenCV BGR을 모델이 학습한 RGB 순서로 바꾼다.

```cpp
input.substract_mean_normalize(nullptr, NORMALIZE);
```

픽셀 `0~255`를 `0~1` 범위로 정규화한다.

## 10.5 추론

```cpp
extractor.input(INPUT_BLOB_NAME, input);
extractor.extract(OUTPUT_BLOB_NAME, output);
```

NCNN 모델의 입력 blob에 텐서를 넣고 출력 blob을 꺼낸다. blob 이름이
모델과 다르면 명확한 오류를 반환한다.

## 10.6 YOLO 출력 해석

각 예측은 다음 값으로 구성된다.

```text
centerX, centerY, width, height, smokeScore, fireScore...
```

현재 코드는 `SMOKE_CLASS_ID` 점수만 읽는다. fire 클래스는 OpenCV
화염 검출이 담당하므로 YOLO fire 결과는 사용하지 않는다.

## 10.7 원본 좌표 복원

```cpp
x1 = (x1 - leftPadding) / scale;
y1 = (y1 - topPadding) / scale;
```

모델 좌표에서 letterbox 여백을 빼고 resize 배율로 나눠 원본 영상
좌표로 되돌린다.

## 10.8 NMS

점수가 높은 박스부터 보관한다. 이미 보관한 박스와 IoU가 임계값보다
높으면 같은 연기를 중복 검출한 것으로 보고 제거한다.

---

## 11. `SmokeDetectionRuntime.cpp`

## 11.1 채널 상태

`ChannelState`는 채널마다 다음 값을 따로 유지한다.

```text
최신 대기 프레임
프레임 ID와 epoch
직전 움직임 Gray 영상
최신 검출 결과
연속 양성 hits
연속 음성 misses
추적 중인 연기 박스
처리 시간
```

모델 `detector_`는 `ChannelState`가 아니라 `Impl`에 하나만 있다.

## 11.2 제출 제한

```cpp
if (sourceTime < channel.nextAcceptedTime)
    return false;
```

각 채널의 1초 추론 간격이 지나지 않았으면 제출을 거절한다.

## 11.3 Round-robin

```cpp
const size_t index =
    (roundRobinCursor_ + offset) % channels_.size();
```

CH1만 계속 처리하지 않고 마지막 처리 채널 다음부터 순서대로 대기 작업을
찾는다.

## 11.4 움직임 검증

NCNN 박스 내부에서 이전 프레임과 현재 프레임 차이를 계산한다.

```cpp
absdiff(previousGray, currentGray, motionMask);
threshold(motionMask, motionMask, MOTION_PIXEL_THRESHOLD, 255, THRESH_BINARY);
```

현재 설정은 `REQUIRE_MOTION_VERIFICATION=false`이므로 움직임이 없다고
NCNN 결과를 버리지는 않는다. 수치와 라벨은 계산되며, 향후 현장 튜닝으로
활성화할 수 있다.

## 11.5 같은 위치의 연기 누적

```cpp
const bool sameRegion =
    !hasTrackedSmokeBox ||
    belongsToTrackedRegion(previousBox, currentBox);
```

IoU 또는 중심거리로 같은 영역인지 확인한다.

```cpp
if (positiveHits >= CONFIRM_HITS)
    smokeDetected = true;
```

현재 `CONFIRM_HITS=2`이므로 같은 영역의 양성 결과가 두 번 이어져야 최종
연기 상태가 된다.

## 11.6 해제

```cpp
if (consecutiveMisses >= RELEASE_MISSES)
{
    smokeDetected = false;
    positiveHits = 0;
}
```

현재 두 번 연속 음성이면 연기 상태와 추적 박스를 초기화한다.

---

## 12. `PersonMetadataReceiver.cpp`

## 12.1 XML namespace 제거 비교

```cpp
const size_t colon = qualifiedName.rfind(':');
const string local = colon == string::npos
    ? qualifiedName
    : qualifiedName.substr(colon + 1);
```

`tt:Frame`에서 `tt:`를 제거해 `Frame`으로 비교한다.

## 12.2 사람 명칭 정규화

```cpp
value.erase(remove_if(value.begin(), value.end(), ...), value.end());
```

공백, `_`, `-`를 제거한다. 따라서 `HumanBody`, `human_body`,
`human-body`를 같은 의미로 볼 수 있다.

## 12.3 한 프레임 파싱

```cpp
while (true)
{
    const size_t objectStart =
        findOpeningTag(frameBlock, "Object", ...);
    ...
}
```

한 XML `Frame` 안의 모든 `Object`를 순회한다.

```cpp
if (!containsPersonClass(block, confidence))
    continue;
```

사람이 아닌 객체는 여기서 건너뛴다.

```cpp
extractNumber(boxTag, "left", person.left);
extractNumber(boxTag, "top", person.top);
extractNumber(boxTag, "right", person.right);
extractNumber(boxTag, "bottom", person.bottom);
```

네 좌표 중 하나라도 없거나 숫자가 아니면 잘못된 박스로 보고 버린다.

## 12.4 Transformation

```cpp
left = left * scaleX + translateX;
top = top * scaleY + translateY;
```

ONVIF `Transformation`이 있으면 원시 분석 좌표를 정규화 좌표로 먼저
변환한다.

## 12.5 ONVIF 좌표를 영상 좌표로

```cpp
x1 = (min(left, right) + 1.0) * 0.5 * width;
y1 = (1.0 - max(top, bottom)) * 0.5 * height;
```

ONVIF의 `-1~1`, Y축 위쪽 증가 좌표를 OpenCV의 왼쪽 위 원점, Y축 아래쪽
증가 좌표로 바꾼다.

## 12.6 완성된 XML만 꺼내기

RTSP 패킷 경계와 XML 태그 경계는 같지 않을 수 있다. `<Frame>`의 절반만
한 번에 들어올 수도 있다.

```cpp
xmlBuffer_.append(data, size);
for (const string& frame : extractCompleteFrames(xmlBuffer_))
    publish(parseFrame(frame));
```

바이트를 버퍼에 계속 붙이고 완전한 `<Frame>...</Frame>`만 파싱한다.

## 12.7 FFmpeg 인자

```cpp
"-allowed_media_types", "data",
"-map", "0:d:0?",
"-codec", "copy",
"-f", "data"
```

영상 decode는 하지 않고 XML data 패킷만 복사한다. 따라서 사람 인식
연산은 카메라가 수행하고 Raspberry Pi는 문자열 파싱만 한다.

## 12.8 Windows 종료

```cpp
if (processHandle_)
    TerminateProcess(processHandle_, 0);
```

FFmpeg가 `ReadFile()` 대기 중이어도 프로세스를 끝내 파이프 읽기를
깨운다.

## 12.9 현재 재연결 대기

```cpp
this_thread::sleep_for(
    chrono::milliseconds(RECONNECT_MS));
```

이 잠은 `stop()`으로 즉시 깨울 수 없다. 4채널을 순차 `join()`하면 종료
지연이 누적될 수 있다. 즉시 종료 개선 시 `condition_variable`로 바꿔야
하는 부분이다.

---

## 13. `FireView.cpp`

## 13.1 화염·연기 박스

```cpp
const Scalar color =
    detection.type == DetectionType::FIRE
    ? Scalar(0, 0, 255)
    : Scalar(255, 255, 0);
```

OpenCV 색상 순서는 RGB가 아니라 BGR이다.

- `(0,0,255)`: 빨간색 화염
- `(255,255,0)`: 청록색 연기

## 13.2 사람 박스

```cpp
const Scalar color(0, 255, 0);
rectangle(display, person.box, color, 2);
```

사람은 초록색으로 그린다.

```cpp
snprintf(label, ..., "PERSON %s %.2f",
    objectId, confidence);
```

카메라 객체 ID와 WiseAI confidence를 박스 위에 표시한다.

## 13.3 Fresh 박스만 표시

```cpp
if (fireSnapshot.boxIsFresh)
    drawDetectionResult(...);

if (smokeSnapshot.boxIsFresh)
    drawDetectionResult(...);
```

오래된 화염·연기 박스는 그리지 않는다. 사람 박스는
`PersonMetadataReceiver::snapshot()` 단계에서 이미 stale 결과가
제거된다.

## 13.4 4분할

```cpp
hconcat(tiles[0], tiles[1], top);
hconcat(tiles[2], tiles[3], bottom);
vconcat(top, bottom, grid);
```

CH1+CH2를 윗줄, CH3+CH4를 아랫줄로 합친다.

## 13.5 종료 키

```cpp
const char key = static_cast<char>(waitKey(delayMs));
return key != 'q' && key != 27;
```

- `q`: false 반환
- ASCII 27인 `ESC`: false 반환
- 그 외: 계속 실행

키는 즉시 인식한다. 이후 늦게 꺼지는 것은 메인 루프 뒤의 스레드 정리
시간 때문이다.

---

## 14. 빌드 코드

### 14.1 CMake 실행 파일

```cmake
add_executable(fire_detection
    main.cpp
    CameraStream.cpp
    ...
    PersonMetadataReceiver.cpp
)
```

여기에 없는 `.cpp`는 CMake 실행 파일에 컴파일되지 않는다.

### 14.2 NCNN 조건

```cmake
target_compile_definitions(
    fire_detection PRIVATE SMOKE_HAS_NCNN=1)
target_link_libraries(fire_detection PRIVATE ncnn)
```

NCNN을 찾았을 때만 실제 연기 추론 코드를 활성화하고 라이브러리를 링크한다.

### 14.3 모델 복사

```cmake
add_custom_command(TARGET fire_detection POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/models"
        "$<TARGET_FILE_DIR:fire_detection>/models"
    VERBATIM)
```

빌드 성공 후 실행 파일 옆에 모델 디렉터리를 자동 복사한다.

---

## 15. 실제 한 프레임의 전체 이동

```text
1. CameraStream::readLoop()
   RTSP에서 frame을 읽고 latestFrame_에 저장

2. ConsoleFireApplication::run()
   getLatestFrame()으로 새 frame 확인

3. FireDetectionRuntime::submitFrame()
   화염 분석 간격이 되었으면 최신 프레임 복사

4. SmokeDetectionRuntime::submitFrame()
   해당 채널의 연기 추론 간격이 되었으면 최신 프레임 복사

5. PersonMetadataReceiver::snapshot()
   별도 XML 스트림의 최신 사람 좌표 반환

6. FireDetectionRuntime worker
   OpenCV FlameDetector 실행

7. SmokeDetectionRuntime worker
   round-robin으로 NCNN SmokeDetector 실행

8. poll()
   완료된 최신 화염·연기 결과와 freshness 계산

9. FireView::showGrid()
   원본 영상 위에 화염, 연기, 사람 박스를 그리고 2×2 결합

10. reportStateChange()/reportPersonBoxes()
    서버 연동에 사용할 상태와 좌표를 콘솔로 출력
```

---

## 16. `CheckWiseAiMetadata.ps1`

### 16.1 입력 매개변수

```powershell
param(
    [Parameter(Mandatory = $true)]
    [string]$CameraIp,
    [string]$Username = "admin",
    [int]$CaptureSeconds = 0
)
```

카메라 IP는 반드시 받고, 사용자 이름은 생략하면 `admin`을 사용한다.
`CaptureSeconds=0`이면 스트림 존재만 확인하고 XML 파일은 만들지 않는다.

### 16.2 FFmpeg 설치 확인

```powershell
$ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
$ffmpeg = Get-Command ffmpeg -ErrorAction SilentlyContinue
```

PATH에서 실행 파일을 찾는다. 둘 중 하나라도 없으면 카메라 연결을
시도하기 전에 명확한 오류로 종료한다.

### 16.3 비밀번호 입력

```powershell
$securePassword = Read-Host "Camera password" -AsSecureString
```

콘솔에 비밀번호 문자를 그대로 표시하지 않는다.

FFmpeg URL을 만들 때 잠시 일반 문자열이 필요하므로 `SecureString`을
BSTR로 변환한다. `finally`에서 반드시 메모리를 0으로 덮고 해제한다.

```powershell
[Runtime.InteropServices.Marshal]::ZeroFreeBSTR($passwordPointer)
```

### 16.4 URL 인코딩

```powershell
$encodedPassword = [Uri]::EscapeDataString($plainPassword)
```

비밀번호에 `!`, `@`, `#` 같은 URL 특수문자가 있어도 주소의 구분 문자로
잘못 해석되지 않도록 percent encoding한다.

### 16.5 4채널 점검

```powershell
foreach ($channel in 0..3)
```

한 번 실행하면 `/0`부터 `/3`까지 모두 검사한다.

`ffprobe`는 `codec_type=data` 스트림 존재 여부를 JSON으로 보여준다.
`CaptureSeconds`가 1 이상이면 FFmpeg가 data 트랙을 그대로
`wiseai_chN_sample.xml`에 저장한다.

이 스크립트는 점검용이며 C++ 실행 중에는 호출되지 않는다.

---

## 17. 수정할 때 지켜야 할 핵심

1. `DetectionTypes.h` 구조를 바꾸면 사용하는 모든 런타임과 화면 코드를
   함께 확인한다.
2. 모델 입력 크기를 바꾸면 letterbox와 NCNN export 크기가 일치해야 한다.
3. `SMOKE_CLASS_ID`는 모델 클래스 순서와 일치해야 한다.
4. 사람 박스는 카메라 채널 영상과 같은 RTSP 프로필에서 받아야 좌표가
   맞는다.
5. 검출 스레드 안에서 GUI 함수를 호출하지 않는다.
6. 메인 스레드가 내부 detector 변수를 직접 읽지 않고 snapshot을 사용한다.
7. 카메라 재연결 시 이전 epoch 결과를 버린다.
8. `.param`, `.bin`은 항상 한 쌍으로 배포한다.
9. 공개 Git에는 실제 카메라 비밀번호를 넣지 않는다.
10. 종료 지연을 수정할 때 스레드를 강제로 떼어내지 말고, 먼저 중단 신호를
    보내고 안전하게 `join()`한다.
