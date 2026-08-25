#pragma once

#include <cstddef>

// 입력 모드: 1이면 동영상 파일, 0이면 RTSP 카메라를 사용한다.

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

// 최대 채널 수를 변경해야 하는 다른 실행 경로를 위한 빌드 설정이다.
// 현재 콘솔 실행 경로는 IP 하나를 입력받아 카메라의 0~3번 채널을 연다.
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

// 한화비전 WiseAI 사람 객체 메타데이터
// PNM-C16083RVQ의 RTSP 데이터 트랙(ONVIF XML)은 OpenCV VideoCapture로 읽을 수
// 없으므로 FFmpeg 보조 프로세스가 영상 디코딩 없이 메타데이터만 복사한다.
namespace person_metadata_config
{
    // false로 바꾸면 WiseAI 수신기를 시작하지 않는다. 변경 후 재빌드가 필요하다.
    constexpr bool ENABLED = true;
    constexpr bool RTSP_USE_TCP = true;

    // Windows는 PATH의 ffmpeg.exe, Raspberry Pi는 ffmpeg 패키지를 사용한다.
    constexpr const char* FFMPEG_EXECUTABLE = "ffmpeg";

    // 첫 번째 데이터 트랙을 선택한다. '?'는 트랙이 없어도 영상 실행을 막지 않는다.
    constexpr const char* STREAM_MAP = "0:d:0?";
    // ffmpeg 6.0에서 -stimeout 이 제거돼 버전마다 옵션명이 갈린다.    
    // 소켓 정지 대비는 RECONNECT_MS(2초) 재시작 로직이 담당하므로 미사용.
    // constexpr int SOCKET_TIMEOUT_US = 5000000;  
    constexpr int RECONNECT_MS = 2000;
    constexpr int FRESH_MS = 1500;
    constexpr double MIN_CONFIDENCE = 0.30;

    constexpr std::size_t BUFFER_LIMIT_BYTES = 4U * 1024U * 1024U;
    constexpr std::size_t BUFFER_KEEP_BYTES = 512U * 1024U;

    // 표시 박스에만 적용하는 여백이며 카메라 원본 좌표는 변경하지 않는다.
    constexpr double BOX_PADDING_X_RATIO = 0.03;
    constexpr double BOX_PADDING_TOP_RATIO = 0.02;
    constexpr double BOX_PADDING_BOTTOM_RATIO = 0.02;

    // Qt/서버 연동용 사람 좌표를 콘솔에 출력하는 최소 주기다.
    constexpr int REPORT_INTERVAL_MS = 500;
}

// 화면 및 디버그
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

// OpenCV 화염 검출 설정
#ifndef FLAME_ENABLE_SKIN_REJECTION
#define FLAME_ENABLE_SKIN_REJECTION 1
#endif

namespace flame_config
{
    // 360p 입력을 확대하지 않고 원본 픽셀로 분석한다.
    constexpr int ANALYSIS_WIDTH = 640;
    constexpr int ANALYSIS_HEIGHT = 360;

    // 채널당 약 6 FPS로 검출한다. 영상 수신/표시는 계속 최신 프레임을 사용한다.
    constexpr int DETECTION_INTERVAL_MS = 167;

    // 4채널은 이미 채널 단위로 병렬 처리하므로 OpenCV 내부 스레드는 1개로 제한한다.
    constexpr int OPENCV_NUM_THREADS = 1;

    // MOG2가 배경을 학습하는 동안의 오검출을 막되 시작 지연은 약 1초로 제한한다.
    constexpr int BACKGROUND_WARMUP_FRAMES = 6;
    constexpr double MOG2_LEARNING_RATE = 0.012;

    // 1.0: 640x360 전체 크기에서 Gray MOG2 수행.
    // 품질을 더 희생해 CPU를 줄일 때만 0.5로 변경한다.
    constexpr double MOG2_SCALE = 1.0;

    // RGB 기반 화염색 마스크의 기준값이다.
    constexpr int ORIGINAL_RED_THRESHOLD = 150;
    constexpr double ORIGINAL_SATURATION_COEFFICIENT = 0.40;

    constexpr double MIN_CONTOUR_AREA = 6.0;
    constexpr int MAX_CONTOURS_TO_ANALYZE = 10;
    constexpr int TINY_CANDIDATE_AREA = 700;
    constexpr int CONFIRM_HITS = 5;
    constexpr int MAX_TRACK_MISSES = 5;
    constexpr double NEW_TRACK_MIN_SCORE = 0.43;
    constexpr double CONFIRM_MIN_SCORE = 0.50;

    // 별도 학습한 SVM XML을 사용할 때만 true로 바꾼다.
    constexpr bool USE_OPTIONAL_SVM = false;
    constexpr const char* OPTIONAL_SVM_PATH = "flame_svm.xml";
}

// Persistent, warning-only camera blur/cover diagnosis used by the detection runtime.
namespace camera_health_config
{
    // Run this low-priority diagnosis at 1 FPS on a small image. Fire/smoke inference
    // keeps its own frequency and is not delayed by repeated blur calculations.
    constexpr int ANALYSIS_WIDTH = 160;
    constexpr int ANALYSIS_INTERVAL_MS = 1000;
    constexpr double BLUR_LAPLACIAN_VARIANCE_THRESHOLD = 20.0;
    constexpr int DARK_PIXEL_THRESHOLD = 12;
    constexpr int BRIGHT_PIXEL_THRESHOLD = 248;
    constexpr double DARK_PIXEL_RATIO_THRESHOLD = 0.70;
    constexpr double BRIGHT_PIXEL_RATIO_THRESHOLD = 0.85;
    constexpr int WARNING_PERSISTENCE_MS = 3000;
    constexpr int HEALTHY_CLEAR_MS = 1000;
}

// 공개 D-Fire YOLOv8n 연기 NCNN 설정
namespace smoke_config
{
    // 640x360 원본을 확대하지 않고 위아래 12픽셀씩 패딩해 640x384로 추론한다.
    constexpr int INPUT_WIDTH = 640;
    constexpr int INPUT_HEIGHT = 384;
    constexpr int MAX_CHANNELS = 4;

    // 공유 NCNN 워커의 목표 처리 간격이다. 런타임은 이 값에 실제 채널 수를
    // 곱해 채널별 제출 주기를 계산한다(1채널=1초, 4채널=4초).
    constexpr int SHARED_WORKER_INTERVAL_MS = 1000;
    constexpr int MAX_CHANNEL_INFERENCE_INTERVAL_MS =
        SHARED_WORKER_INTERVAL_MS * MAX_CHANNELS;
#if defined(__arm__) || defined(__aarch64__)
    // 영상 수신과 OpenCV 화염 검출에 CPU를 남기기 위해 NCNN 스레드를 제한한다.
    constexpr int NCNN_NUM_THREADS = 2;
#else
    constexpr int NCNN_NUM_THREADS = 3;
#endif

    // 공개 D-Fire 모델은 0: smoke, 1: fire이며 여기서는 smoke만 사용한다.
    constexpr int SMOKE_CLASS_ID = 0;
    constexpr float CONFIDENCE_THRESHOLD = 0.25F;
    constexpr float RAW_CANDIDATE_THRESHOLD = CONFIDENCE_THRESHOLD;
    constexpr float NMS_THRESHOLD = 0.45F;

    // 화면 대부분을 덮는 박스는 회색 벽/바닥 같은 정적 배경 오검출일 가능성이
    // 높다. 이런 대형 박스에만 움직임 검증을 강제해 작은 실제 연기는 유지한다.
    constexpr float LARGE_BOX_AREA_RATIO = 0.80F;

    // false이면 움직임 통계는 라벨에만 표시되고 YOLO 결과를 차단하지 않는다.
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

    // 연속 양성 박스가 같은 영역일 때만 hits를 누적해 서로 다른 오검출의 합산을 막는다.
    constexpr float TRACK_MIN_IOU = 0.08F;
    constexpr float TRACK_MAX_CENTER_DISTANCE_RATIO = 0.45F;
    constexpr float MERGE_EXPANSION_RATIO = 0.12F;
    constexpr std::size_t MAX_TRACKS_PER_CHANNEL = 16;
    // 4채널에서는 채널별 결과가 약 4초마다 나오므로 3회 확인은 최초 표시를
    // 12초 이상 늦춘다. 모델 임계값을 통과한 첫 결과부터 표시하고 센서 융합은
    // 서버의 최종 경보 단계에서 담당한다.
    constexpr int CONFIRM_HITS = 1;
    // 확정 전 후보는 빠르게 정리하고, 확정된 연기는 마지막 실제 검출부터 5초간 유지한다.
    constexpr int RELEASE_MISSES = 2;
    constexpr int RELEASE_HOLD_MS = 5000;
    constexpr int RELEASE_HOLD_RESULTS =
        (RELEASE_HOLD_MS + MAX_CHANNEL_INFERENCE_INTERVAL_MS - 1) /
        MAX_CHANNEL_INFERENCE_INTERVAL_MS;
    // 결과가 완성될 때 이미 지나치게 오래된 프레임이면 사용하지 않는다.
    constexpr int MAX_PIPELINE_LATENCY_MS = 2500;
    // 정상 완료 결과는 같은 채널의 다음 결과(약 4초)가 올 때까지 유지한다.
    constexpr int RESULT_FRESH_MS = 5000;
    constexpr int BOX_FRESH_MS = 5000;

    constexpr const char* MODEL_PARAM_PATH =
        "models/smoke_yolov8n_round2_full10_20260825_640x384_ncnn_model/model.ncnn.param";
    constexpr const char* MODEL_BIN_PATH =
        "models/smoke_yolov8n_round2_full10_20260825_640x384_ncnn_model/model.ncnn.bin";
    constexpr const char* INPUT_BLOB_NAME = "in0";
    constexpr const char* OUTPUT_BLOB_NAME = "out0";
}
