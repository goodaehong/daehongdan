#pragma once
#include <string>
#include "../detection/IgnoreRegionFilter.h"

// Qt 수신 스레드 ↔ 채널별 worker 스레드 사이의 ROI 전달용 보관소.
// FireDetectionRuntime이 worker() 지역변수라 수신 쪽에서 직접 못 건드림.
// worker는 버전만 보고, 바뀌었을 때만 복사해 간다.
//
// applyTo에 따라 화재용·연기용 config를 나눠서 보관한다.
// worker는 둘을 각각의 런타임에 넣기만 하면 됨.

// Qt 메시지 한 줄을 파싱·검증해서 저장. 성공하면 true.
// 실패 시 reason에 사유가 담긴다 (Qt ack의 reason 필드용)
bool RoiStore_Apply(const std::string& line, int* chOut, std::string* reason);

// worker용. Version은 락 없이 읽는다
int  RoiStore_Version(int ch);
void RoiStore_Get(int ch, IgnoreRegionConfig& fire, IgnoreRegionConfig& smoke);
double RoiStore_Threshold(int ch);        

// server/roi/roi_config.json 저장·복원
bool RoiStore_Save();
void RoiStore_Load();

// Qt push·query_result 응답용. 받은 원문을 그대로 돌려준다
std::string RoiStore_ToJson(int ch);