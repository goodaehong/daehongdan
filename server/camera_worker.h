#pragma once
#include "shared_state.h"
#include "detection/SmokeDetectionRuntime.h"

// 채널 하나를 맡는 입력 워커. RTSP 연결 → 프레임 읽기 → 감지 코어에 제출 →
// 결과를 FrameStore·DetectionState 에 채운다. 판단은 하지 않는다.
// 끊기면 스스로 재연결한다.
void cameraWorker(int ch, FrameStore& store, Link& link, SmokeDetectionRuntime& smoke);
