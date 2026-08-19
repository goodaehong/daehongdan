#pragma once
#include <string>

// Qt가 올린 평면도(base64 PNG)를 파일로 저장하고, 변환 결과를 보관한다.
// EvacPlanner(태호님) 연동 전이라 지금은 저장·복원까지만 한다.

// set_floor_map 한 줄을 파싱해서 이미지 저장.
// 성공하면 true, 실패 시 reason에 사유 (Qt 응답의 reason 필드용)
bool FloorMapStore_Apply(const std::string& line, std::string* reason);

// query_result / floor_map_result 본문. 아직 변환 결과가 없으면 빈 문자열
std::string FloorMapStore_ToJson();

// 서버 시작 시 저장된 결과 복원 (파일 없으면 그냥 넘어감)
void FloorMapStore_Load();