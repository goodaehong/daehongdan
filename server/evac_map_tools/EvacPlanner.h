#pragma once

// 대피경로 계산. 평면도 이미지에서 벽 · 전광판 · 출구를 추출해 60 × 60 격자로 만들고,
// 다익스트라로 전광판별 출구 경로를 구한다.
// 화재 좌표를 넣으면 그 구역을 통행 불가로 찍고 우회 경로를 다시 탐색한다.

#include <vector>
#include <string>

// 격자 한 변의 크기 (비트맵은 GRID_SIZE x GRID_SIZE)
// HUB75 테두리가 상하좌우 각 2px씩이라 실제 지도는 60x60만 씀 (STM32 main.c와 반드시 일치해야 함)
// server/detection/GridCoordinateMapper.h의 감지 좌표 격자(60)와 통일 — 화재 감지 위치와
// 대피경로가 같은 좌표계를 쓰도록 팀에서 60으로 확정 (2026-08-19).
constexpr int GRID_SIZE = 60;

// 좌표 구조체
struct Point {
    int y, x;
    bool operator==(const Point& other) const { return y == other.y && x == other.x; }
    bool operator!=(const Point& other) const { return !(*this == other); }
};

// 화재 정보 구조체
struct FireCell {
    int x = 0;
    int y = 0;
    int radius = 0;
};

// [핵심 함수] 평면도 이미지를 넣어 전체 파이프라인 실행 후 경로 반환.
// routes[i] = i번째 경로의 꺾이는 지점 좌표들. 두 점 사이는 수신측(STM32)이 직선으로 이어 그린다.
// 순서는 (전광판1 -> 출구1,2,...), (전광판2 -> 출구1,2,...) 순이고,
// 도달 불가 경로는 빈 행으로 들어간다.
std::vector<std::vector<Point>> processFloorPlan(const std::string& imagePath, const std::vector<FireCell>& fires = {});

// 평면도 이미지에서 장애물 비트맵(1=벽, 0=통로)만 계산해 반환.
// 실패 시 빈 vector 반환.
std::vector<std::vector<int>> getEvacBitmap(const std::string& imagePath);

// 전광판 좌표들. 벽에 인접한 통행 가능한 칸으로 스냅된 값.
// 순서는 processFloorPlan()의 경로 순서와 동일 (전광판 i = displays[i]).
std::vector<Point> getEvacDisplays(const std::string& imagePath);

// 출구 좌표들. 항상 바깥 경계선 위(y==0 || y==GRID_SIZE-1 || x==0 || x==GRID_SIZE-1).
// 순서는 processFloorPlan()의 경로 순서와 동일 (출구 j = exits[j]).
std::vector<Point> getEvacExits(const std::string& imagePath);

// 비트맵/전광판/출구 좌표를 STM32 main.c의
// "USER CODE BEGIN EVAC_DATA" ~ "USER CODE END EVAC_DATA" 사이에 C 배열로 자동 반영.
// (HUB75_EvacMap[GRID_SIZE][GRID_SIZE], EvacDisplays[][2], EvacExits[][2] 전부 갱신)
// main.c에서 그 마커를 못 찾으면 아무것도 안 건드리고 false 리턴 (수동 반영 필요하다는 뜻).
bool exportToMainC(const std::string& imagePath, const std::string& mainCPath);