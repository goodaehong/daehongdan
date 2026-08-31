#pragma once

// 채널별 보정 상태 보관소. 워커가 기록한 보정 단계와 실시간 마커 인식 상태를
// 모아 두었다가 Qt 조회에 응답한다. 재로드 요청도 여기를 통해 워커에 전달된다.

#include <string>
#include "detection/GridCoordinateMapper.h"   // ArucoMappingStatus

// 보정 단계. 파일이 하나씩 갖춰질수록 다음 단계로 올라간다
enum class CalibStage {
    Unknown,      // 아직 워커가 시작 전
    NoBoard,      // 마커 배치도 없음
    NoLens,       // 렌즈 왜곡 보정값 없음
    NoHomography, // 좌표 변환표 없음
    Ready         // 정상
};

// 서버 시작·재로드 시 워커가 호출. 어느 단계까지 갔는지 기록
void CalibStore_SetStage(int ch, CalibStage stage, const std::string& detail);

// 워커가 매 프레임 호출. 마커 개수·오차 등 실시간 값 갱신
void CalibStore_SetLive(int ch, const ArucoMappingStatus& st);

// Qt가 재로드를 요청하면 +1. 워커가 값이 바뀐 걸 보고 다시 읽는다
// (ROI가 쓰는 방식과 동일 — 평소엔 정수 하나만 비교하고 지나간다)
void CalibStore_RequestReload(int ch);
int  CalibStore_ReloadVersion(int ch);

// query target="calib_status" 응답 본문 (4채널 배열)
std::string CalibStore_ToJson();