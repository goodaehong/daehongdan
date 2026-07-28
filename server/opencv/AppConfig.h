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
#define VIDEO_FILE_PATH R"(C:\Users\3-19\Desktop\PJ\fire&smoke\fire_test\88240-602915792_medium.mp4)"
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
#define RTSP_PROFILE_PATH "/0/profile10/media.smp"
#endif

#ifndef RTSP_PROFILE_SUFFIX
#define RTSP_PROFILE_SUFFIX "/profile10/media.smp"
#endif

// Up to four independent 360p camera streams. Leave an IP empty to be
// prompted at startup; pressing Enter on an empty prompt disables that channel.
#ifndef RTSP_CAMERA_COUNT
#define RTSP_CAMERA_COUNT 4
#endif
#ifndef RTSP_CAMERA_IP_1
#define RTSP_CAMERA_IP_1 ""
#endif
#ifndef RTSP_CAMERA_IP_2
#define RTSP_CAMERA_IP_2 ""
#endif
#ifndef RTSP_CAMERA_IP_3
#define RTSP_CAMERA_IP_3 ""
#endif
#ifndef RTSP_CAMERA_IP_4
#define RTSP_CAMERA_IP_4 ""
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

    // At 6 FPS, 20 frames blocked all fire results for about 3.3 seconds.
    // Six frames still let MOG2 settle while keeping startup latency near one second.
    constexpr int BACKGROUND_WARMUP_FRAMES = 6;
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
    constexpr int CONFIRM_HITS = 5;
    constexpr int MAX_TRACK_MISSES = 5;
    constexpr double NEW_TRACK_MIN_SCORE = 0.43;
    constexpr double CONFIRM_MIN_SCORE = 0.50;

    // 선택적 SVM XML 모델. 모델이 없으면 false 유지.
    constexpr bool USE_OPTIONAL_SVM = false;
    constexpr const char* OPTIONAL_SVM_PATH = "flame_svm.xml";
}

// ==================================================
// YOLO11n smoke-only NCNN configuration
// ==================================================
namespace smoke_config
{
    // The camera supplies 640x360. Keep all source pixels and letterbox only
    // the height to a stride-32 NCNN tensor (12 px top + 12 px bottom).
    constexpr int INPUT_WIDTH = 640;
    constexpr int INPUT_HEIGHT = 384;
    constexpr int MAX_CHANNELS = 4;

    // One shared model handles every channel. Each channel may submit at most
    // one newest frame per second; queued old frames are overwritten.
    constexpr int INFERENCE_INTERVAL_MS = 1000;
#if defined(__arm__) || defined(__aarch64__)
    // Leave CPU capacity for capture, OpenCV fire detection and the application.
    constexpr int NCNN_NUM_THREADS = 2;
#else
    constexpr int NCNN_NUM_THREADS = 3;
#endif

    // Public D-Fire YOLOv8n model classes are 0: smoke, 1: fire.
    // Fire is ignored because the OpenCV flame detector handles it separately.
    constexpr int SMOKE_CLASS_ID = 0;
    constexpr float CONFIDENCE_THRESHOLD = 0.25F;
    constexpr float RAW_CANDIDATE_THRESHOLD = CONFIDENCE_THRESHOLD;
    constexpr float NMS_THRESHOLD = 0.45F;

    // First evaluate the stronger public model on its raw score. The temporal
    // motion measurements remain available for labels and a later field-tuned
    // gate, but they do not alter or reject model output in this baseline.
    constexpr bool REQUIRE_MOTION_VERIFICATION = false;
    constexpr int MOTION_ANALYSIS_WIDTH = 320;
    constexpr int MOTION_PIXEL_THRESHOLD = 14;
    constexpr float MOTION_MIN_RATIO = 0.012F;
    constexpr float MOTION_FULL_RATIO = 0.10F;
    constexpr float MOTION_MAX_VALID_RATIO = 0.35F;
    constexpr float GLOBAL_MOTION_MAX_RATIO = 0.20F;
    constexpr float MOTION_INNER_MARGIN_RATIO = 0.18F;
    constexpr float MOTION_MIN_INNER_RATIO = 0.008F;
    constexpr int MOTION_GRID_COLUMNS = 4;
    constexpr int MOTION_GRID_ROWS = 3;
    constexpr float MOTION_MIN_CELL_RATIO = 0.008F;
    constexpr int MOTION_MIN_ACTIVE_CELLS = 3;
    constexpr float MOTION_MAX_BONUS = 0.00F;

    // Consecutive positives must also refer to approximately the same region.
    // This prevents unrelated weak boxes in different parts of a channel from
    // accumulating into one smoke alarm.
    constexpr float TRACK_MIN_IOU = 0.08F;
    constexpr float TRACK_MAX_CENTER_DISTANCE_RATIO = 0.45F;
    constexpr int CONFIRM_HITS = 2;
    constexpr int RELEASE_MISSES = 2;
    constexpr int RESULT_FRESH_MS = 2500;
    constexpr int BOX_FRESH_MS = 1500;

    constexpr const char* MODEL_PARAM_PATH =
        "models/smoke_yolov8n_public_640x384_ncnn_model/model.ncnn.param";
    constexpr const char* MODEL_BIN_PATH =
        "models/smoke_yolov8n_public_640x384_ncnn_model/model.ncnn.bin";
    constexpr const char* INPUT_BLOB_NAME = "in0";
    constexpr const char* OUTPUT_BLOB_NAME = "out0";
}
