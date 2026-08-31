#pragma once

// Qt가 올린 평면도(base64 PNG)를 파일로 저장하고, 변환 결과를 보관한다.
// 결과는 Qt 응답용 JSON 글자와, 전광판 전송용 숫자 두 형태로 들고 있다.

#include <string>
#include <vector>
#include "EvacPlanner.h"   // Point 구조체 (좌표는 {y, x} 순서)

// 전광판 하나에서 출구 하나로 가는 경로
struct EvacRoute {
    int displayId = 0;             // 1부터
    int exitId = 0;                // 1부터
    std::vector<Point> waypoints;  // 꺾이는 지점들. 도달 불가면 비어 있음
};

// set_floor_map 한 줄을 파싱해서 이미지 저장 + 변환.
// 성공하면 true, 실패 시 reason에 사유 (Qt 응답의 reason 필드용)
bool FloorMapStore_Apply(const std::string& line, std::string* reason);

// query_result / floor_map_result 본문. 아직 변환 결과가 없으면 빈 문자열
std::string FloorMapStore_ToJson();

// 서버 시작 시 저장된 결과 복원 (파일 없으면 그냥 넘어감)
void FloorMapStore_Load();

// ── 전광판 전송용 ──
// 변환 결과가 있으면 true. 없으면 경로를 못 보내므로 호출부가 먼저 확인해야 한다
bool FloorMapStore_HasRoutes();

// 전광판 개수 (경로를 전광판별로 묶어 쓰기 위함)
int FloorMapStore_DisplayCount();

// 지정 전광판의 경로들. 출구 순서대로 담긴다
std::vector<EvacRoute> FloorMapStore_RoutesFor(int displayId);

// 화재를 반영해 다시 계산한 경로. fires 가 비면 저장된 기본 경로와 같다   
std::vector<EvacRoute> FloorMapStore_RoutesForFires(int displayId,
                                                    const std::vector<FireCell>& fires);   