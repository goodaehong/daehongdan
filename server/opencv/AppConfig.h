#pragma once

// ==================================================
// 입력 소스
// ==================================================
// 1: 동영상 파일
// 0: RTSP 카메라

#ifndef USE_VIDEO_FILE
#define USE_VIDEO_FILE 0
#endif

#ifndef VIDEO_FILE_LOOP
#define VIDEO_FILE_LOOP 1
#endif

#ifndef VIDEO_FILE_PATH
#define VIDEO_FILE_PATH R"(C:\Users\3-19\Desktop\fire_test.mp4)"
#endif

#ifndef RTSP_USE_UDP
#define RTSP_USE_UDP 0
#endif

#ifndef RTSP_USERNAME
#define RTSP_USERNAME "admin"
#endif

#ifndef RTSP_PASSWORD
#define RTSP_PASSWORD "5hanwha!"
#endif

#ifndef RTSP_PROFILE_PATH
#define RTSP_PROFILE_PATH "/0/profile2/media.smp"
#endif

// ==================================================
// 화면 및 디버그
// ==================================================
// 라즈베리파이에서는 GUI를 자동으로 끄고, Windows 테스트에서는 켠다.
#ifndef FIRE_ENABLE_GUI
#if defined(__arm__) || defined(__aarch64__)
#define FIRE_ENABLE_GUI 0
#else
#define FIRE_ENABLE_GUI 1
#endif
#endif

#ifndef FIRE_DEBUG_VIEW
#define FIRE_DEBUG_VIEW 0
#endif

#ifndef FIRE_DEBUG_TILE_WIDTH
#define FIRE_DEBUG_TILE_WIDTH 240
#endif

#ifndef FIRE_DEBUG_TILE_HEIGHT
#define FIRE_DEBUG_TILE_HEIGHT 135
#endif

// ==================================================
// GitHub flame-detection-system 기반 검출기 설정
// ==================================================
#ifndef FLAME_ENABLE_SKIN_REJECTION
#define FLAME_ENABLE_SKIN_REJECTION 1
#endif

namespace flame_config
{
    // 360p 입력을 960x540으로 다시 키우지 않는다.
    constexpr int ANALYSIS_WIDTH = 640;
    constexpr int ANALYSIS_HEIGHT = 360;

    // 채널당 약 6 FPS로 검출한다. 영상 수신/표시는 계속 최신 프레임을 사용한다.
    constexpr int DETECTION_INTERVAL_MS = 167;

    // 4채널은 이미 채널 단위로 병렬 처리하므로 OpenCV 내부 스레드는 1개로 제한한다.
    constexpr int OPENCV_NUM_THREADS = 1;

    constexpr int BACKGROUND_WARMUP_FRAMES = 20;
    constexpr double MOG2_LEARNING_RATE = 0.012;

    // 1.0: 640x360 전체 크기에서 Gray MOG2 수행.
    // 품질을 더 희생해 CPU를 줄일 때만 0.5로 변경한다.
    constexpr double MOG2_SCALE = 1.0;

    // 원 공개 코드의 RGB 화염색 판정 계열
    constexpr int ORIGINAL_RED_THRESHOLD = 150;
    constexpr double ORIGINAL_SATURATION_COEFFICIENT = 0.40;

    constexpr double MIN_CONTOUR_AREA = 6.0;
    constexpr int MAX_CONTOURS_TO_ANALYZE = 10;
    constexpr int TINY_CANDIDATE_AREA = 700;
    constexpr int CONFIRM_HITS = 3;
    constexpr int MAX_TRACK_MISSES = 5;
    constexpr double NEW_TRACK_MIN_SCORE = 0.43;
    constexpr double CONFIRM_MIN_SCORE = 0.50;

    // 선택적 SVM XML 모델. 모델이 없으면 false 유지.
    constexpr bool USE_OPTIONAL_SVM = false;
    constexpr const char* OPTIONAL_SVM_PATH = "flame_svm.xml";
}