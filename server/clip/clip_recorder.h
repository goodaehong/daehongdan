#pragma once

// 이벤트 영상 클립 저장. 평소 직전 3초를 채널별 링 버퍼에 쌓아두고,
// 경보가 나면 이후 10초까지 합쳐 13초짜리 mp4 로 저장한다.
// 경보 이후만 녹화하면 원인이 되는 장면이 빠지기 때문이다.

#include <functional>
#include <string>
#include <opencv2/core.hpp>

// 저장 완료 알림 (저장 스레드에서 호출됨). incidentId 로 DB 행을 찾아 경로를 채운다
// ts = 클립 시작 시각. 클립이 담은 시간대를 알아야 맞는 행에만 박을 수 있다   
using ClipSavedFn = std::function<void(long incidentId, long ts,
                                       const std::string& path)>;           

// 저장 폴더 생성 + 저장 스레드 시작
void ClipRecorder_Init(ClipSavedFn onSaved);

// 감지 워커가 매 프레임 호출. 채널별 링 버퍼(직전 3초)에 쌓아둔다
void ClipRecorder_Push(int ch, const cv::Mat& frame);

// 이벤트 시 호출 → 직전 3초 + 이후 10초를 mp4 하나로 저장.
// 그 채널이 이미 녹화 중이면 false (한 사태에 클립이 여러 개 생기는 것 방지)
bool ClipRecorder_Start(int ch, const std::string& zone, long ts, long incidentId);

// 파일 하나를 통째로 base64 문자열로. 실패 시 빈 문자열              
// mp4·jpg 구분 없이 쓴다 (Qt가 파이 안의 파일을 직접 못 열어서 내용을 실어 보냄)
std::string ClipRecorder_ReadBase64(const std::string& path); 

void ClipRecorder_Shutdown();