#include "EvacPlanner.h"
#include <opencv2/opencv.hpp>
#include <fstream>
#include <iostream>
#include <queue>
#include <algorithm>

constexpr int OUT_SIZE = GRID_SIZE;

// 내부 전용 자료구조
struct Location {
    int id;
    Point p;
};

struct Route {
    int start_id;
    int exit_id;
    std::vector<Point> waypoints;
    bool is_reachable;
};

constexpr int kObstacleDilatePx = 0;
constexpr int kObstacleThreshold = 125;

// 마커 색상 정의
struct Marker { std::string name; cv::Scalar lower; cv::Scalar upper; };
const Marker kDisplayMarker = {"Display", cv::Scalar(9, 97, 225),  cv::Scalar(69, 157, 255)};
const Marker kExitMarker    = {"Exit",    cv::Scalar(0, 200, 151), cv::Scalar(59, 255, 211)};
const cv::Scalar kWhiteLower(200, 200, 200), kWhiteUpper(255, 255, 255);

const int dy[] = { -1, 1, 0, 0 };
const int dx[] = { 0, 0, -1, 1 };

// --- 내부 유틸리티 함수들 ---
cv::Mat makeMask(const cv::Mat& img, const cv::Scalar& lower, const cv::Scalar& upper) {
    cv::Mat mask;
    cv::inRange(img, lower, upper, mask);
    return mask;
}

cv::Mat toGridMask(const cv::Mat& mask, int dilatePx) {
    cv::Mat dilated = mask;
    if (dilatePx > 0) {
        cv::Mat kernel = cv::Mat::ones(dilatePx, dilatePx, CV_8U);
        cv::dilate(mask, dilated, kernel);
    }
    cv::Mat resized, binary;
    cv::resize(dilated, resized, cv::Size(OUT_SIZE, OUT_SIZE), 0, 0, cv::INTER_AREA);
    cv::threshold(resized, binary, kObstacleThreshold, 255, cv::THRESH_BINARY);
    return binary;
}

std::vector<cv::Point> findMarkerGridPoints(const cv::Mat& img, const Marker& marker) {
    cv::Mat mask = makeMask(img, marker.lower, marker.upper);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    std::vector<cv::Point> points;
    for (const auto& c : contours) {
        cv::Moments m = cv::moments(c);
        if (m.m00 <= 0) continue;
        int gx = (int)std::round((m.m10 / m.m00) * OUT_SIZE / img.cols);
        int gy = (int)std::round((m.m01 / m.m00) * OUT_SIZE / img.rows);
        points.push_back(cv::Point(gx, gy));
    }
    return points;
}

cv::Point snapToBoundary(cv::Point p) {
    int distLeft = p.x, distRight = OUT_SIZE - 1 - p.x, distTop = p.y, distBottom = OUT_SIZE - 1 - p.y;
    int minDist = std::min({distLeft, distRight, distTop, distBottom});
    if (minDist == distLeft) p.x = 0;
    else if (minDist == distRight) p.x = OUT_SIZE - 1;
    else if (minDist == distTop) p.y = 0;
    else p.y = OUT_SIZE - 1;
    return p;
}

cv::Point snapAdjacentToWall(const std::vector<std::vector<int>>& grid, cv::Point p) {
    const int ddx[4] = {1, -1, 0, 0};
    const int ddy[4] = {0, 0, 1, -1};
    for (int r = 0; r < OUT_SIZE; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                int x = p.x + dx, y = p.y + dy;
                if (x < 0 || x >= OUT_SIZE || y < 0 || y >= OUT_SIZE) continue;
                if (grid[y][x] != 0) continue;
                for (int k = 0; k < 4; k++) {
                    int nx = x + ddx[k], ny = y + ddy[k];
                    if (nx < 0 || nx >= OUT_SIZE || ny < 0 || ny >= OUT_SIZE) continue;
                    if (grid[ny][nx] == 1) return cv::Point(x, y);
                }
            }
        }
    }
    return p;
}

// 1. 비트맵 파일 출력 (1과 0)
void saveBitmapText(const std::vector<std::vector<int>>& grid, const std::string& path) {
    std::ofstream f(path);
    for (int y = 0; y < OUT_SIZE; y++) {
        for (int x = 0; x < OUT_SIZE; x++) {
            f << grid[y][x] << (x == OUT_SIZE - 1 ? "" : " ");
        }
        f << "\n";
    }
}

// 2. 디버깅용 파일 출력 (#과 .)
void saveDebugText(const std::vector<std::vector<int>>& grid, const std::vector<Location>& displays,
                   const std::vector<Location>& exits, const std::string& path) {
    std::ofstream f(path);
    for (int y = 0; y < OUT_SIZE; y++) {
        for (int x = 0; x < OUT_SIZE; x++) {
            char c = (grid[y][x] != 0) ? '#' : '.';
            for (const auto& d : displays) if (d.p.x == x && d.p.y == y) c = 'S'; // Start
            for (const auto& e : exits) if (e.p.x == x && e.p.y == y) c = 'E';    // Exit
            f << c;
        }
        f << "\n";
    }
}

// 3. PNG 미리보기 출력
void saveColorPreview(const std::vector<std::vector<int>>& grid, const std::vector<Location>& displays,
                      const std::vector<Location>& exits, const std::string& path, int scale = 8) {
    cv::Mat color(OUT_SIZE, OUT_SIZE, CV_8UC3, cv::Scalar(0, 0, 0));
    for (int y = 0; y < OUT_SIZE; y++) {
        for (int x = 0; x < OUT_SIZE; x++) {
            if (grid[y][x] != 0) color.at<cv::Vec3b>(y, x) = cv::Vec3b(255, 255, 255);
        }
    }
    for (const auto& d : displays) color.at<cv::Vec3b>(d.p.y, d.p.x) = cv::Vec3b(0, 140, 255);
    for (const auto& e : exits) color.at<cv::Vec3b>(e.p.y, e.p.x) = cv::Vec3b(0, 255, 0);

    cv::Mat preview;
    cv::resize(color, preview, cv::Size(OUT_SIZE * scale, OUT_SIZE * scale), 0, 0, cv::INTER_NEAREST);
    cv::imwrite(path, preview);
}

// 4. 경로 데이터 파일 출력
void saveRoutesText(const std::vector<Route>& routes, const std::string& path) {
    std::ofstream f(path);
    for (const auto& r : routes) {
        f << "Start ID: " << r.start_id << " -> Exit ID: " << r.exit_id << "\n";
        if (!r.is_reachable) {
            f << "Status: Unreachable\n\n";
            continue;
        }
        f << "Waypoints(" << r.waypoints.size() << "): ";
        for (size_t i = 0; i < r.waypoints.size(); ++i) {
            f << "(" << r.waypoints[i].y << "," << r.waypoints[i].x << ")";
            if (i < r.waypoints.size() - 1) f << " -> ";
        }
        f << "\n\n";
    }
}

// 연속된 직선 구간을 접어 꺾이는 지점만 남긴다
std::vector<Point> compressToWaypoints(const std::vector<Point>& path) {
    if (path.size() < 3) return path;
    std::vector<Point> waypoints{ path.front() };
    for (size_t i = 1; i + 1 < path.size(); ++i) {
        int py = path[i].y - path[i - 1].y, px = path[i].x - path[i - 1].x;
        int ny = path[i + 1].y - path[i].y, nx = path[i + 1].x - path[i].x;
        if (py != ny || px != nx) waypoints.push_back(path[i]);
    }
    waypoints.push_back(path.back());
    return waypoints;
}

// 경로 계산 알고리즘
std::vector<Route> calculateRoutes(const std::vector<std::vector<int>>& grid, const std::vector<Location>& starts, const std::vector<Location>& exits) {
    std::vector<Route> all_routes;
    for (const auto& start : starts) {
        std::vector<std::vector<Point>> parent(OUT_SIZE, std::vector<Point>(OUT_SIZE, { -1, -1 }));
        std::queue<Point> q;
        q.push(start.p);
        parent[start.p.y][start.p.x] = start.p;

        while (!q.empty()) {
            Point curr = q.front();
            q.pop();
            for (int i = 0; i < 4; ++i) {
                int ny = curr.y + dy[i], nx = curr.x + dx[i];
                if (ny >= 0 && ny < OUT_SIZE && nx >= 0 && nx < OUT_SIZE) {
                    if (grid[ny][nx] == 0 && parent[ny][nx].y == -1) {
                        parent[ny][nx] = curr;
                        q.push({ ny, nx });
                    }
                }
            }
        }

        for (const auto& exit : exits) {
            Route route;
            route.start_id = start.id;
            route.exit_id = exit.id;
            if (exit.p.y < 0 || exit.p.y >= OUT_SIZE || exit.p.x < 0 || exit.p.x >= OUT_SIZE || parent[exit.p.y][exit.p.x].y == -1) {
                route.is_reachable = false;
            } else {
                route.is_reachable = true;
                std::vector<Point> full_path;
                Point curr = exit.p;
                while (curr != start.p) {
                    full_path.push_back(curr);
                    curr = parent[curr.y][curr.x];
                }
                full_path.push_back(start.p);
                std::reverse(full_path.begin(), full_path.end());
                route.waypoints = compressToWaypoints(full_path);
            }
            all_routes.push_back(route);
        }
    }
    return all_routes;
}

// 평면도 분석 결과 (비트맵 + 전광판/출구 위치)
struct FloorPlan {
    std::vector<std::vector<int>> grid;
    std::vector<Location> starts;
    std::vector<Location> exits;
};

bool analyzeFloorPlan(const std::string& imagePath, FloorPlan& out) {
    cv::Mat img = cv::imread(imagePath);
    if (img.empty()) {
        std::cerr << "[Error] 이미지를 불러올 수 없습니다: " << imagePath << "\n";
        return false;
    }

    // 1. OpenCV 이미지 처리 마스크 생성
    cv::Mat whiteMask = makeMask(img, kWhiteLower, kWhiteUpper);
    cv::Mat orangeMask = makeMask(img, kDisplayMarker.lower, kDisplayMarker.upper);
    cv::Mat greenMask = makeMask(img, kExitMarker.lower, kExitMarker.upper);
    cv::Mat notObstacle;
    cv::bitwise_or(whiteMask, orangeMask, notObstacle);
    cv::bitwise_or(notObstacle, greenMask, notObstacle);
    cv::Mat obstacleMask;
    cv::bitwise_not(notObstacle, obstacleMask);
    
    cv::Mat obstacleGrid = toGridMask(obstacleMask, kObstacleDilatePx);
    std::vector<std::vector<int>> grid(OUT_SIZE, std::vector<int>(OUT_SIZE, 0));
    for (int y = 0; y < OUT_SIZE; y++) {
        for (int x = 0; x < OUT_SIZE; x++) {
            if (obstacleGrid.at<uint8_t>(y, x) > 0) grid[y][x] = 1;
        }
    }

    // 2. 출구(Exit) 찾기 및 경계 스냅
    auto rawExits = findMarkerGridPoints(img, kExitMarker);
    std::vector<Location> exits;
    int exitId = 1;
    for (auto& e : rawExits) {
        e = snapToBoundary(e);
        exits.push_back({exitId++, {e.y, e.x}}); // cv::Point(x,y) -> Point(y,x)
    }

    // 테두리 벽 마감 (출구 제외)
    for (int x = 0; x < OUT_SIZE; x++) { grid[0][x] = 1; grid[OUT_SIZE - 1][x] = 1; }
    for (int y = 0; y < OUT_SIZE; y++) { grid[y][0] = 1; grid[y][OUT_SIZE - 1] = 1; }
    for (const auto& e : exits) grid[e.p.y][e.p.x] = 0;

    // 3. 전광판(Start) 찾기 및 스냅
    auto rawDisplays = findMarkerGridPoints(img, kDisplayMarker);
    std::vector<Location> starts;
    int startId = 1;
    for (auto& d : rawDisplays) {
        d = snapAdjacentToWall(grid, d);
        starts.push_back({startId++, {d.y, d.x}});
    }

    out.grid = std::move(grid);
    out.starts = std::move(starts);
    out.exits = std::move(exits);
    return true;
}

// --- 메인 파이프라인 함수 ---
std::vector<std::vector<Point>> processFloorPlan(const std::string& imagePath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};

    // 4. 요구사항 파일 저장
    saveBitmapText(fp.grid, "evac_bitmap.txt");
    saveDebugText(fp.grid, fp.starts, fp.exits, "evac_debug.txt");
    saveColorPreview(fp.grid, fp.starts, fp.exits, "evac_preview.png");

    // 5. 경로 탐색 및 데이터 저장
    std::vector<Route> finalRoutes = calculateRoutes(fp.grid, fp.starts, fp.exits);
    saveRoutesText(finalRoutes, "evac_routes.txt");

    // 6. STM32 전송용으로 좌표만 남긴 2차원 배열 리턴
    std::vector<std::vector<Point>> result;
    result.reserve(finalRoutes.size());
    for (const auto& r : finalRoutes) result.push_back(r.waypoints);

    std::cout << "[시스템] 파이프라인 완료. 파일 저장(txt, png) 및 경로 탐색 성공.\n";
    return result;
}

// --- 비트맵만 반환 (STM32 전송용) ---
std::vector<std::vector<int>> getEvacBitmap(const std::string& imagePath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};
    return fp.grid;
}

// --- 전광판 좌표만 반환 (Qt 전송용) ---
std::vector<Point> getEvacDisplays(const std::string& imagePath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};
    std::vector<Point> points;
    points.reserve(fp.starts.size());
    for (const auto& s : fp.starts) points.push_back(s.p);
    return points;
}

// --- 출구 좌표만 반환 (Qt 전송용) ---
std::vector<Point> getEvacExits(const std::string& imagePath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};
    std::vector<Point> points;
    points.reserve(fp.exits.size());
    for (const auto& e : fp.exits) points.push_back(e.p);
    return points;
}