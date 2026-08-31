// EvacPlanner.h 구현. 이미지 분석 · 격자 변환 · 경로 탐색을 담당한다.

#include "EvacPlanner.h"
#include <opencv2/opencv.hpp>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <queue>
#include <algorithm>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX   // windows.h가 min/max 매크로를 정의해서 std::min/std::max를 깨뜨리는 것 방지
#endif
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

constexpr int OUT_SIZE = GRID_SIZE;

// 실행 파일 자신이 위치한 디렉터리의 절대경로(끝에 구분자 포함)를 반환.
// 어느 작업 디렉터리(cwd)에서 실행하든 evac_bitmap.txt 등 산출물이 항상
// 실행 파일과 같은 폴더(=evac_map_tools/)에 생성되게 하기 위함. 실패 시 빈 문자열(=cwd에 저장, 기존 동작).
static std::string getExecutableDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return "";
    std::string path(buf, len);
    size_t pos = path.find_last_of("\\/");
    return (pos == std::string::npos) ? "" : path.substr(0, pos + 1);
#else
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = '\0';
    std::string path(buf);
    size_t pos = path.find_last_of('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos + 1);
#endif
}

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

struct State {
    int cost, y, x, dir;
    bool operator>(const State& other) const { return cost > other.cost; }
};

struct DirPoint {
    int y, x, dir;
};

constexpr int kObstacleDilatePx = 0;
constexpr int kObstacleThreshold = 120;

// 마커 색상 정의
struct Marker { std::string name; cv::Scalar lower; cv::Scalar upper; };
const Marker kDisplayMarker = {"Display", cv::Scalar(9, 97, 225),  cv::Scalar(69, 157, 255)};
const Marker kExitMarker    = {"Exit",    cv::Scalar(0, 200, 151), cv::Scalar(59, 255, 211)};
const cv::Scalar kWhiteLower(200, 200, 200), kWhiteUpper(255, 255, 255);

const int dy[] = { -1, 1, 0, 0 };
const int dx[] = { 0, 0, -1, 1 };

// --- 내부 유틸리티 및 파일 출력 함수들 (복구됨) ---
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
            f << "Status: Unreachable (Blocked by Fire or Wall)\n\n";
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

// 화재/경로가 격자에 실제로 반영된 상태를 전광판별로 확인하는 디버그 맵 (*=화재위험구역, F=화재중심, 1=이 전광판 경로)
void savePerStartDebugs(const std::vector<std::vector<int>>& grid, const std::vector<Location>& starts, const std::vector<Route>& routes, const std::vector<FireCell>& fires, const std::string& outDir) {
    for (const auto& start : starts) {
        std::vector<std::string> debugMap(OUT_SIZE, std::string(OUT_SIZE, '.'));
        for (int y = 0; y < OUT_SIZE; ++y) {
            for (int x = 0; x < OUT_SIZE; ++x) {
                if (grid[y][x] == 1) debugMap[y][x] = '#';
            }
        }
        for (const auto& fire : fires) {
            int min_x = fire.x - fire.radius - 2;
            int max_x = fire.x + fire.radius + 2;
            int min_y = fire.y - (2 * fire.radius) - 2; 
            int max_y = fire.y + 2;
            for (int y = std::max(0, min_y); y <= std::min(OUT_SIZE - 1, max_y); ++y) {
                for (int x = std::max(0, min_x); x <= std::min(OUT_SIZE - 1, max_x); ++x) {
                    debugMap[y][x] = '*';
                }
            }
            if (fire.y >= 0 && fire.y < OUT_SIZE && fire.x >= 0 && fire.x < OUT_SIZE) {
                debugMap[fire.y][fire.x] = 'F';
            }
        }
        for (const auto& r : routes) {
            if (r.start_id == start.id && r.is_reachable && !r.waypoints.empty()) {
                for (size_t i = 0; i < r.waypoints.size() - 1; ++i) {
                    Point curr = r.waypoints[i];
                    Point next = r.waypoints[i + 1];
                    
                    int dy = (next.y > curr.y) ? 1 : ((next.y < curr.y) ? -1 : 0);
                    int dx = (next.x > curr.x) ? 1 : ((next.x < curr.x) ? -1 : 0);
                    
                    Point p = curr;
                    while (p != next) {
                        if (debugMap[p.y][p.x] != '*' && debugMap[p.y][p.x] != 'F') {
                            debugMap[p.y][p.x] = '1';
                        }
                        p.y += dy;
                        p.x += dx;
                    }
                }
            }
        }
        std::string filename = outDir + "evac_debug_start_" + std::to_string(start.id) + ".txt";
        std::ofstream f(filename);
        for (int y = 0; y < OUT_SIZE; y++) {
            f << debugMap[y] << "\n";
        }
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

// --- 경로 계산 알고리즘 (화재 회피 및 우회) ---
std::vector<Route> calculateRoutes(const std::vector<std::vector<int>>& grid, const std::vector<Location>& starts, const std::vector<Location>& exits, const std::vector<FireCell>& fires) {
    
    std::vector<std::vector<int>> localGrid = grid;
    for (const auto& fire : fires) {
        int min_x = fire.x - fire.radius - 2;
        int max_x = fire.x + fire.radius + 2;
        int min_y = fire.y - (2 * fire.radius) - 2; 
        int max_y = fire.y + 2;
        for (int y = std::max(0, min_y); y <= std::min(OUT_SIZE - 1, max_y); ++y) {
            for (int x = std::max(0, min_x); x <= std::min(OUT_SIZE - 1, max_x); ++x) {
                localGrid[y][x] = 1;
            }
        }
    }

    std::vector<std::vector<int>> distMap(OUT_SIZE, std::vector<int>(OUT_SIZE, 999999));
    std::queue<Point> wq;
    for (int i = 0; i < OUT_SIZE; ++i) {
        for (int j = 0; j < OUT_SIZE; ++j) {
            if (localGrid[i][j] == 1) {
                distMap[i][j] = 0;
                wq.push({i, j});
            }
        }
    }
    while (!wq.empty()) {
        Point curr = wq.front();
        wq.pop();
        for (int k = 0; k < 4; ++k) {
            int ny = curr.y + dy[k], nx = curr.x + dx[k];
            if (ny >= 0 && ny < OUT_SIZE && nx >= 0 && nx < OUT_SIZE && localGrid[ny][nx] == 0) {
                if (distMap[ny][nx] > distMap[curr.y][curr.x] + 1) {
                    distMap[ny][nx] = distMap[curr.y][curr.x] + 1;
                    wq.push({ny, nx});
                }
            }
        }
    }

    std::vector<Route> all_routes;
    for (const auto& start : starts) {
        std::vector<std::vector<std::vector<int>>> cost(OUT_SIZE, std::vector<std::vector<int>>(OUT_SIZE, std::vector<int>(4, 999999)));
        std::vector<std::vector<std::vector<DirPoint>>> parent(OUT_SIZE, std::vector<std::vector<DirPoint>>(OUT_SIZE, std::vector<DirPoint>(4, {-1, -1, -1})));
        std::priority_queue<State, std::vector<State>, std::greater<State>> pq;

        for (int i = 0; i < 4; ++i) {
            int ny = start.p.y + dy[i];
            int nx = start.p.x + dx[i];
            if (ny >= 0 && ny < OUT_SIZE && nx >= 0 && nx < OUT_SIZE && localGrid[ny][nx] == 0) {
                int wallDist = distMap[ny][nx];
                int wallPenalty = (wallDist < 4) ? (4 - wallDist) * 15 : 0; 
                int initialCost = 1 + wallPenalty;
                
                cost[ny][nx][i] = initialCost;
                parent[ny][nx][i] = {start.p.y, start.p.x, -1};
                pq.push({initialCost, ny, nx, i});
            }
        }

        while (!pq.empty()) {
            State curr = pq.top();
            pq.pop();

            if (curr.cost > cost[curr.y][curr.x][curr.dir]) continue;

            for (int next_dir = 0; next_dir < 4; ++next_dir) {
                if ((curr.dir == 0 && next_dir == 1) || (curr.dir == 1 && next_dir == 0) ||
                    (curr.dir == 2 && next_dir == 3) || (curr.dir == 3 && next_dir == 2)) {
                    continue;
                }

                int ny = curr.y + dy[next_dir], nx = curr.x + dx[next_dir];
                if (ny >= 0 && ny < OUT_SIZE && nx >= 0 && nx < OUT_SIZE && localGrid[ny][nx] == 0) {
                    
                    int wallDist = distMap[ny][nx];
                    int wallPenalty = (wallDist < 4) ? (4 - wallDist) * 12 : 0;
                    int turnPenalty = (curr.dir != next_dir) ? 50 : 0;
                    
                    int nextCost = curr.cost + 1 + wallPenalty + turnPenalty;

                    if (nextCost < cost[ny][nx][next_dir]) {
                        cost[ny][nx][next_dir] = nextCost;
                        parent[ny][nx][next_dir] = {curr.y, curr.x, curr.dir};
                        pq.push({nextCost, ny, nx, next_dir});
                    }
                }
            }
        }

        for (const auto& exit : exits) {
            Route route;
            route.start_id = start.id;
            route.exit_id = exit.id;
            
            int min_cost = 999999;
            int best_dir = -1;
            
            if (exit.p.y >= 0 && exit.p.y < OUT_SIZE && exit.p.x >= 0 && exit.p.x < OUT_SIZE) {
                for (int i = 0; i < 4; ++i) {
                    if (cost[exit.p.y][exit.p.x][i] < min_cost) {
                        min_cost = cost[exit.p.y][exit.p.x][i];
                        best_dir = i;
                    }
                }
            }

            if (best_dir == -1) {
                route.is_reachable = false; 
            } else {
                route.is_reachable = true;
                std::vector<Point> full_path;
                
                DirPoint curr_dp = {exit.p.y, exit.p.x, best_dir};
                while (curr_dp.dir != -1) {
                    full_path.push_back({curr_dp.y, curr_dp.x});
                    curr_dp = parent[curr_dp.y][curr_dp.x][curr_dp.dir];
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

    auto rawExits = findMarkerGridPoints(img, kExitMarker);
    std::vector<Location> exits;
    int exitId = 1;
    for (auto& e : rawExits) {
        e = snapToBoundary(e);
        exits.push_back({exitId++, {e.y, e.x}});
    }

    for (int x = 0; x < OUT_SIZE; x++) { grid[0][x] = 1; grid[OUT_SIZE - 1][x] = 1; }
    for (int y = 0; y < OUT_SIZE; y++) { grid[y][0] = 1; grid[y][OUT_SIZE - 1] = 1; }
    for (const auto& e : exits) grid[e.p.y][e.p.x] = 0;

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

// --- 메인 파이프라인 함수 (버그 픽스 및 디버그 로직 복원) ---
std::vector<std::vector<Point>> processFloorPlan(const std::string& imagePath, const std::vector<FireCell>& fires) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};

    // 실행 파일이 있는 폴더(=evac_map_tools/) 기준 고정 경로에 저장 -> 어느 cwd에서 실행해도 결과물 위치가 항상 동일함
    const std::string outDir = getExecutableDir();
    saveBitmapText(fp.grid, outDir + "evac_bitmap.txt");
    saveDebugText(fp.grid, fp.starts, fp.exits, outDir + "evac_debug.txt");
    saveColorPreview(fp.grid, fp.starts, fp.exits, outDir + "evac_preview.png");

    std::vector<Route> finalRoutes = calculateRoutes(fp.grid, fp.starts, fp.exits, fires);
    saveRoutesText(finalRoutes, outDir + "evac_routes.txt");
    savePerStartDebugs(fp.grid, fp.starts, finalRoutes, fires, outDir);

    std::vector<std::vector<Point>> result;
    result.reserve(finalRoutes.size());
    for (const auto& r : finalRoutes) {
        // [수정 포인트] 도달 불가 경로도 빈 배열로 유지하여 서버의 인덱싱 보호
        if (r.is_reachable) {
            result.push_back(r.waypoints);
        } else {
            result.push_back(std::vector<Point>{}); 
        }
    }

    std::cout << "[시스템] 파이프라인 완료. 파일 저장(txt, png) 및 경로 탐색 성공.\n";
    return result;
}

// --- 비트맵 반환 API ---
std::vector<std::vector<int>> getEvacBitmap(const std::string& imagePath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};
    return fp.grid;
}

// --- 전광판 좌표 반환 API ---
std::vector<Point> getEvacDisplays(const std::string& imagePath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};
    std::vector<Point> points;
    points.reserve(fp.starts.size());
    for (const auto& s : fp.starts) points.push_back(s.p);
    return points;
}

// --- 출구 좌표 반환 API ---
std::vector<Point> getEvacExits(const std::string& imagePath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return {};
    std::vector<Point> points;
    points.reserve(fp.exits.size());
    for (const auto& e : fp.exits) points.push_back(e.p);
    return points;
}

// --- main.c 자동 반영: 비트맵/전광판/출구를 STM32 C 배열 텍스트로 직렬화 ---
static std::string gridToCArray(const std::vector<std::vector<int>>& grid) {
    std::ostringstream out;
    out << "// 0=빈공간(통행가능), 1=장애물(벽/기계/구조물 전부 포함, 구분 없음) - 전광판/출구 위치는 별도 배열(EvacDisplays/EvacExits) 참고\n";
    out << "static const uint8_t HUB75_EvacMap[" << OUT_SIZE << "][" << OUT_SIZE << "] = {\n";
    for (int y = 0; y < OUT_SIZE; y++) {
        out << "  {";
        for (int x = 0; x < OUT_SIZE; x++) {
            out << grid[y][x];
            if (x != OUT_SIZE - 1) out << ",";
        }
        out << "},\n";
    }
    out << "};\n";
    return out.str();
}

static std::string pointsToCArray(const std::vector<Location>& locs, const std::string& varName, const std::string& countName) {
    std::ostringstream out;
    out << "static const uint8_t " << varName << "[][2] = {\n";
    for (const auto& l : locs)
        out << "  {" << l.p.x << "," << l.p.y << "},\n";
    out << "};\n";
    out << "#define " << countName << " " << locs.size() << "\n";
    return out.str();
}

static bool patchMainC(const std::string& mainCPath, const std::string& arrayText) {
    std::ifstream in(mainCPath);
    if (!in) {
        std::cerr << "[Error] main.c를 못 읽음: " << mainCPath << "\n";
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    in.close();

    const std::string beginMarker = "/* USER CODE BEGIN EVAC_DATA */";
    const std::string endMarker   = "/* USER CODE END EVAC_DATA */";
    size_t beginPos = content.find(beginMarker);
    size_t endPos   = content.find(endMarker);
    if (beginPos == std::string::npos || endPos == std::string::npos || endPos < beginPos) {
        std::cerr << "[Error] main.c에서 EVAC_DATA 마커를 못 찾음 - 수동으로 반영해야 함\n";
        return false;
    }

    size_t insertStart = beginPos + beginMarker.size();
    std::string newContent = content.substr(0, insertStart) + "\n" + arrayText + content.substr(endPos);

    std::ofstream out(mainCPath);
    out << newContent;
    return true;
}

bool exportToMainC(const std::string& imagePath, const std::string& mainCPath) {
    FloorPlan fp;
    if (!analyzeFloorPlan(imagePath, fp)) return false;

    std::ostringstream out;
    out << gridToCArray(fp.grid) << "\n";
    out << pointsToCArray(fp.starts, "EvacDisplays", "EVAC_DISPLAY_COUNT") << "\n";
    out << pointsToCArray(fp.exits, "EvacExits", "EVAC_EXIT_COUNT") << "\n";

    if (!patchMainC(mainCPath, out.str())) return false;

    std::cout << "[시스템] main.c 갱신 완료: " << mainCPath
               << " (전광판 " << fp.starts.size() << "개, 출구 " << fp.exits.size() << "개)\n";
    return true;
}