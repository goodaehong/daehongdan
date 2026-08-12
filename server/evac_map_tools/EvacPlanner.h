#pragma once
#include <vector>
#include <string>

// 격자 한 변의 크기 (비트맵은 GRID_SIZE x GRID_SIZE)
constexpr int GRID_SIZE = 62;

// 좌표 구조체
struct Point {
    int y, x;
    bool operator==(const Point& other) const { return y == other.y && x == other.x; }
    bool operator!=(const Point& other) const { return !(*this == other); }
};

// [핵심 함수] 평면도 이미지를 넣어 전체 파이프라인 실행 후 경로 반환.
// routes[i] = i번째 경로의 꺾이는 지점 좌표들. 두 점 사이는 수신측(STM32)이 직선으로 이어 그린다.
// 순서는 (전광판1 -> 출구1,2,...), (전광판2 -> 출구1,2,...) 순이고,
// 도달 불가 경로는 빈 행으로 들어간다.
std::vector<std::vector<Point>> processFloorPlan(const std::string& imagePath);

// 평면도 이미지에서 장애물 비트맵(1=벽, 0=통로)만 계산해 반환.
// 실패 시 빈 vector 반환.
std::vector<std::vector<int>> getEvacBitmap(const std::string& imagePath);