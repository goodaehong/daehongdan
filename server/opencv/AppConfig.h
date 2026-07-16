#pragma once

// ==================================================
// 입력 소스 선택
// ==================================================
// 1: 동영상 파일 사용
// 0: RTSP 카메라 사용
//
// 카메라로 되돌릴 때는 아래 숫자만 0으로 변경하면 된다.
#define USE_VIDEO_FILE 1

// 동영상 파일 테스트 설정
#define VIDEO_FILE_LOOP 1
#define VIDEO_FILE_PATH R"(C:\Users\3-19\Desktop\PJ\fire&smoke\fire_test\15128643_1080_1920_30fps.mp4)"

// RTSP 카메라 설정
#define RTSP_USE_UDP 0
#define RTSP_USERNAME "admin"
#define RTSP_PASSWORD "5hanwha!"
#define RTSP_PROFILE_PATH "/0/profile2/media.smp"

// ==================================================
// 디버그 화면
// ==================================================
// 1: FireDetector 내부 마스크를 메인 스레드의 디버그 창에 표시
// 0: 디버그 영상 복사 및 디버그 창 비활성화
#define FIRE_DEBUG_VIEW 1