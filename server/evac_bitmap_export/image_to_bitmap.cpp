// 평면도 이미지 -> HUB75 64x64 대피도 데이터 변환 (빌드타임 도구, STM32/라즈베리파이에서 안 돌아감)
//
// 빌드 (라즈베리파이 - OpenCV 이미 설치돼있음):
//   g++ -std=c++17 image_to_bitmap.cpp -o image_to_bitmap `pkg-config --cflags --libs opencv4`
//
// 실행:
//   ./image_to_bitmap <이미지경로>
//
// 판정 규칙: 흰색(배경)도 아니고, 주황(전광판 마커)도 아니고, 연두(출구 마커)도 아니면
//           전부 장애물로 취급 - 벽/파랑/노랑/회색 등 색을 하나하나 등록할 필요 없음.
// 색상 범위(kWhite, kDisplayMarker, kExitMarker)는 원본 이미지 색에 맞춰 조정 필요.
// 결과:
//   evac_bitmap.c    - HUB75_EvacMap[64][64] (0=빈공간,1=장애물) + 전광판/출구 좌표 배열(EvacDisplays/EvacExits)
//                      (전광판/출구 둘 다 여러 개 가능 - 마커 찍은 개수만큼 배열 원소로 들어감)
//   evac_preview.png - 8배 확대 컬러 미리보기 (장애물=흰색, 전광판=주황점, 출구=초록점)
//   evac_debug.txt   - 사람이 읽는 ASCII 격자 (#=장애물, .=빈공간, 5=전광판, E=출구)

#ifdef _WIN32
#define NOMINMAX   // windows.h가 min/max를 매크로로 정의해서 std::min/max랑 충돌하는 것 방지
#include <windows.h>
#endif
#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

constexpr int OUT_SIZE = 62;   // HUB75는 64x64지만, 테두리 2줄(상하좌우 각 1px)은 빨간 점멸 경고용으로 비워둬서 62x62만 실제 맵으로 씀
// 실측 결과: 벽은 raw값 146+, 살려야 하는 좁은 틈은 최대 108~120 - 그 사이인 130이 최적.
// 팽창은 다시 0으로 (걸어도 이 정도로 얇은 벽엔 효과 없고, 오히려 다른 통로를 막을 위험만 있음).
constexpr int kObstacleDilatePx = 0;
constexpr int kObstacleThreshold = 125;

// ── 점 마커(전광판 위치/출구): 영역이 아니라 좌표만 필요. 둘 다 여러 개 있을 수 있음 ──
struct Marker {
    std::string name;
    cv::Scalar lower;
    cv::Scalar upper;
};
// 실제 이미지에서 픽셀 색상 뽑아서 확정한 값 (probe_color.exe로 측정, ±30 여유)
const Marker kDisplayMarker = {"Display", cv::Scalar(9, 97, 225),  cv::Scalar(69, 157, 255)};   // 주황 BGR(39,127,255) - 전광판 위치
const Marker kExitMarker    = {"Exit",    cv::Scalar(0, 200, 151), cv::Scalar(59, 255, 211)};   // 연두 BGR(29,230,181) - 출구
const cv::Scalar kWhiteLower(200, 200, 200), kWhiteUpper(255, 255, 255);                         // 배경(빈 공간)

// 색상 범위로 마스크 생성 (원본 해상도 그대로)
cv::Mat makeMask(const cv::Mat& img, const cv::Scalar& lower, const cv::Scalar& upper)
{
    cv::Mat mask;
    cv::inRange(img, lower, upper, mask);
    return mask;
}

// 원본 해상도 마스크 -> 64x64 이진 마스크 (dilate로 선 보존 후 AREA 축소 + threshold)
cv::Mat toGridMask(const cv::Mat& mask, int dilatePx)
{
    cv::Mat dilated = mask;
    if (dilatePx > 0)
    {
        cv::Mat kernel = cv::Mat::ones(dilatePx, dilatePx, CV_8U);
        cv::dilate(mask, dilated, kernel);
    }
    cv::Mat resized, binary;
    cv::resize(dilated, resized, cv::Size(OUT_SIZE, OUT_SIZE), 0, 0, cv::INTER_AREA);
    cv::threshold(resized, binary, kObstacleThreshold, 255, cv::THRESH_BINARY);
    return binary;
}

// 마커 색상 마스크에서 덩어리(contour)별 중심 좌표를 원본 해상도 기준으로 찾고,
// 64x64 격자 좌표로 축소 변환해서 반환. 마커가 여러 개(출구 2곳 등)여도 각각 분리됨.
std::vector<cv::Point> findMarkerGridPoints(const cv::Mat& img, const Marker& marker)
{
    cv::Mat mask = makeMask(img, marker.lower, marker.upper);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    std::vector<cv::Point> points;
    for (const auto& c : contours)
    {
        cv::Moments m = cv::moments(c);
        if (m.m00 <= 0) continue;   // 너무 작아서 넓이 0인 잡음 덩어리는 무시
        int gx = (int)std::round((m.m10 / m.m00) * OUT_SIZE / img.cols);
        int gy = (int)std::round((m.m01 / m.m00) * OUT_SIZE / img.rows);
        points.push_back(cv::Point(gx, gy));
    }
    return points;
}

// 출구는 항상 바깥 벽 경계 위에 있어야 함 - 반올림 오차로 경계 바로 안쪽에 찍히는 걸 보정
// 4개 경계(왼쪽/오른쪽/위/아래) 중 가장 가까운 쪽으로 그 좌표를 0 또는 63으로 강제 고정
cv::Point snapToBoundary(cv::Point p)
{
    int distLeft = p.x, distRight = OUT_SIZE - 1 - p.x, distTop = p.y, distBottom = OUT_SIZE - 1 - p.y;
    int minDist = std::min({distLeft, distRight, distTop, distBottom});
    if (minDist == distLeft) p.x = 0;
    else if (minDist == distRight) p.x = OUT_SIZE - 1;
    else if (minDist == distTop) p.y = 0;
    else p.y = OUT_SIZE - 1;
    return p;
}

// 전광판은 "벽 셀 자체"가 아니라 "벽이랑 x 또는 y가 정확히 1 차이나는 빈 칸"에 있어야 함
// (벽에 걸쳐있으면 안 되고, 벽 바로 옆에 붙어있는 통행 가능한 칸이어야 함)
// 자기 자신부터 반지름을 넓혀가며, "이 칸 자체는 빈 칸(0)이면서 상하좌우 중 하나가 벽(1)인" 칸을 찾음
cv::Point snapAdjacentToWall(const cv::Mat& grid, cv::Point p)
{
    const int ddx[4] = {1, -1, 0, 0};
    const int ddy[4] = {0, 0, 1, -1};

    for (int r = 0; r < OUT_SIZE; r++)
    {
        for (int dy = -r; dy <= r; dy++)
        {
            for (int dx = -r; dx <= r; dx++)
            {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;   // 반지름 r인 테두리만 검사
                int x = p.x + dx, y = p.y + dy;
                if (x < 0 || x >= OUT_SIZE || y < 0 || y >= OUT_SIZE) continue;
                if (grid.at<uint8_t>(y, x) != 0) continue;   // 이 칸 자체는 반드시 빈 칸이어야 함

                for (int k = 0; k < 4; k++)
                {
                    int nx = x + ddx[k], ny = y + ddy[k];
                    if (nx < 0 || nx >= OUT_SIZE || ny < 0 || ny >= OUT_SIZE) continue;
                    if (grid.at<uint8_t>(ny, nx) == 1) return cv::Point(x, y);   // 인접한 벽 발견
                }
            }
        }
    }
    return p;   // 못 찾으면 원래 좌표 그대로 (사실상 발생 안 함)
}

// 상하좌우 어디에도 붙어있지 않은 고립된 장애물 칸 1개짜리 노이즈 제거
// (원본 이미지 경계의 미세한 색 번짐 등으로 통로 한가운데 외딴 점이 찍히는 것 방지)
void removeIsolatedObstacles(cv::Mat& grid)
{
    cv::Mat copy = grid.clone();
    for (int y = 0; y < OUT_SIZE; y++)
    {
        for (int x = 0; x < OUT_SIZE; x++)
        {
            if (copy.at<uint8_t>(y, x) != 1) continue;
            bool hasNeighbor = false;
            const int ddx[4] = {1, -1, 0, 0};
            const int ddy[4] = {0, 0, 1, -1};
            for (int k = 0; k < 4; k++)
            {
                int nx = x + ddx[k], ny = y + ddy[k];
                if (nx < 0 || nx >= OUT_SIZE || ny < 0 || ny >= OUT_SIZE) continue;
                if (copy.at<uint8_t>(ny, nx) == 1) { hasNeighbor = true; break; }
            }
            if (!hasNeighbor) grid.at<uint8_t>(y, x) = 0;   // 고립된 칸이면 지움
        }
    }
}

// 0/1 값으로만 이루어진 64x64 격자를 C 2차원 배열 텍스트로 직렬화
std::string gridToCArray(const cv::Mat& grid)
{
    std::ostringstream out;
    out << "// 0=빈공간(통행가능), 1=장애물(벽/기계/구조물 전부 포함, 구분 없음) - 전광판/출구 위치는 별도 배열(EvacDisplays/EvacExits) 참고\n";
    out << "const uint8_t HUB75_EvacMap[" << OUT_SIZE << "][" << OUT_SIZE << "] = {\n";
    for (int y = 0; y < OUT_SIZE; y++)
    {
        out << "  {";
        for (int x = 0; x < OUT_SIZE; x++)
        {
            out << (int)grid.at<uint8_t>(y, x);
            if (x != OUT_SIZE - 1) out << ",";
        }
        out << "},\n";
    }
    out << "};\n";
    return out.str();
}

// 마커 좌표 목록(전광판/출구)을 {x,y} 쌍 배열의 C 배열 텍스트로 직렬화
// - 개수만큼 배열 원소로 들어가서, STM32 쪽에서 for문으로 순회 가능 (BFS 최단경로 탐색용)
std::string pointsToCArray(const std::vector<cv::Point>& points, const std::string& varName, const std::string& countName)
{
    std::ostringstream out;
    out << "const uint8_t " << varName << "[][2] = {\n";
    for (const auto& p : points)
        out << "  {" << p.x << "," << p.y << "},\n";
    out << "};\n";
    out << "#define " << countName << " " << points.size() << "\n";
    return out.str();
}

// 컬러 미리보기 PNG 저장: 장애물=흰색, 빈공간=검정, 전광판=주황 점, 출구=초록 점
void saveColorPreview(const cv::Mat& grid, const std::vector<cv::Point>& displays,
                       const std::vector<cv::Point>& exits, const std::string& path, int scale = 8)
{
    cv::Mat color(OUT_SIZE, OUT_SIZE, CV_8UC3, cv::Scalar(0, 0, 0));
    for (int y = 0; y < OUT_SIZE; y++)
    {
        for (int x = 0; x < OUT_SIZE; x++)
        {
            if (grid.at<uint8_t>(y, x) != 0)
                color.at<cv::Vec3b>(y, x) = cv::Vec3b(255, 255, 255);   // 장애물: 흰색
        }
    }
    for (const auto& d : displays) color.at<cv::Vec3b>(d.y, d.x) = cv::Vec3b(0, 140, 255);   // 전광판: 주황
    for (const auto& e : exits) color.at<cv::Vec3b>(e.y, e.x) = cv::Vec3b(0, 255, 0);        // 출구: 초록

    cv::Mat preview;
    cv::resize(color, preview, cv::Size(OUT_SIZE * scale, OUT_SIZE * scale), 0, 0, cv::INTER_NEAREST);
    cv::imwrite(path, preview);
}

// 사람이 읽는 ASCII 격자 저장: #=장애물, .=빈공간, 5=전광판, E=출구
void saveDebugText(const cv::Mat& grid, const std::vector<cv::Point>& displays,
                    const std::vector<cv::Point>& exits, const std::string& path)
{
    std::ofstream f(path);
    for (int y = 0; y < OUT_SIZE; y++)
    {
        f << "row" << y << (y < 10 ? " : " : ": ");
        for (int x = 0; x < OUT_SIZE; x++)
        {
            char c = (grid.at<uint8_t>(y, x) != 0) ? '#' : '.';
            for (const auto& d : displays) if (d.x == x && d.y == y) c = '5';
            for (const auto& e : exits) if (e.x == x && e.y == y) c = 'E';
            f << c;
        }
        f << "\n";
    }
}

// main.c의 "/* USER CODE BEGIN EVAC_DATA */ ~ /* USER CODE END EVAC_DATA */" 사이를
// arrayText로 통째로 교체. main.c 쪽 선언은 static const로 바꿔서 넣음(파일 스코프 전용이라).
// 마커를 못 찾으면 손대지 않고 false만 반환 - main.c가 예전 버전이거나 마커가 지워졌을 때 대비.
bool patchMainC(const std::string& mainCPath, const std::string& arrayText)
{
    std::ifstream in(mainCPath);
    if (!in)
    {
        std::cerr << "main.c를 못 읽음: " << mainCPath << "\n";
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
    if (beginPos == std::string::npos || endPos == std::string::npos || endPos < beginPos)
    {
        std::cerr << "main.c에서 EVAC_DATA 마커를 못 찾음 - 수동으로 붙여넣어야 함\n";
        return false;
    }

    std::string mainCArrayText = arrayText;
    const std::string from = "const uint8_t", to = "static const uint8_t";
    size_t pos = 0;
    while ((pos = mainCArrayText.find(from, pos)) != std::string::npos)
    {
        mainCArrayText.replace(pos, from.size(), to);
        pos += to.size();
    }

    size_t insertStart = beginPos + beginMarker.size();
    std::string newContent = content.substr(0, insertStart) + "\n" + mainCArrayText + content.substr(endPos);

    std::ofstream out(mainCPath);
    out << newContent;
    return true;
}

int main(int argc, char** argv)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // 윈도우 콘솔이 UTF-8 출력을 CP949로 잘못 읽어서 한글 깨지는 것 방지
#endif

    if (argc < 2)
    {
        std::cerr << "사용법: ./image_to_bitmap <이미지경로> [main.c 경로]\n";
        std::cerr << "  main.c 경로를 주면 그 파일의 EVAC_DATA 구역을 자동으로 갱신함(안 주면 evac_bitmap.c만 생성)\n";
        return 1;
    }

    cv::Mat img = cv::imread(argv[1]);
    if (img.empty())
    {
        std::cerr << "이미지를 못 읽음: " << argv[1] << "\n";
        return 1;
    }

    // ── 흰색(배경)도 아니고 전광판(주황)도 아니고 출구(연두)도 아니면 전부 장애물 ──
    cv::Mat whiteMask = makeMask(img, kWhiteLower, kWhiteUpper);
    cv::Mat orangeMask = makeMask(img, kDisplayMarker.lower, kDisplayMarker.upper);
    cv::Mat greenMask = makeMask(img, kExitMarker.lower, kExitMarker.upper);

    cv::Mat notObstacle;
    cv::bitwise_or(whiteMask, orangeMask, notObstacle);
    cv::bitwise_or(notObstacle, greenMask, notObstacle);

    cv::Mat obstacleMask;
    cv::bitwise_not(notObstacle, obstacleMask);   // 나머지 전부(벽/파랑/노랑/회색 등 색 무관) 장애물

    cv::Mat obstacleGrid = toGridMask(obstacleMask, kObstacleDilatePx);
    cv::Mat grid = cv::Mat::zeros(OUT_SIZE, OUT_SIZE, CV_8U);
    for (int y = 0; y < OUT_SIZE; y++)
        for (int x = 0; x < OUT_SIZE; x++)
            if (obstacleGrid.at<uint8_t>(y, x) > 0)
                grid.at<uint8_t>(y, x) = 1;
    removeIsolatedObstacles(grid);   // 통로 한가운데 외딴 노이즈 칸 제거
    std::cout << "[장애물] 처리 완료 (흰색/전광판/출구 제외 전부)\n";

    // ── 출구 좌표 먼저 확정 (경계로 스냅) - 벽 마감할 때 출구 자리만 예외로 뚫어둬야 하니 순서상 먼저 필요 ──
    auto exits = findMarkerGridPoints(img, kExitMarker);
    for (auto& e : exits) e = snapToBoundary(e);   // 출구는 항상 바깥 경계(0 또는 63) 위에 있어야 함
    for (size_t i = 0; i < exits.size(); i++)
        std::cout << "[출구" << (i + 1) << "] 격자 좌표: (" << exits[i].x << ", " << exits[i].y << ")\n";
    if (exits.empty())
        std::cerr << "경고: 출구 마커를 못 찾음 (kExitMarker 색상 범위 확인 필요)\n";

    // ── 바깥 테두리 전체를 벽으로 마감. 출구로 확정된 좌표만 예외로 뚫어둠 ──
    // (원본 이미지의 문 아이콘 등으로 생긴 다른 틈은 전부 막아서, 출구가 아닌 곳으로 경로가 새는 것 방지)
    for (int x = 0; x < OUT_SIZE; x++) { grid.at<uint8_t>(0, x) = 1; grid.at<uint8_t>(OUT_SIZE - 1, x) = 1; }
    for (int y = 0; y < OUT_SIZE; y++) { grid.at<uint8_t>(y, 0) = 1; grid.at<uint8_t>(y, OUT_SIZE - 1) = 1; }
    for (const auto& e : exits) grid.at<uint8_t>(e.y, e.x) = 0;

    // ── 전광판 위치(여러 개 가능) 좌표 찾기 - 벽 마감이 끝난 최종 grid 기준으로 스냅해야 정확함 ──
    auto displays = findMarkerGridPoints(img, kDisplayMarker);
    for (auto& d : displays) d = snapAdjacentToWall(grid, d);   // 벽 셀이 아니라 벽과 1칸 차이나는 빈 칸으로
    for (size_t i = 0; i < displays.size(); i++)
        std::cout << "[전광판" << (i + 1) << "] 격자 좌표: (" << displays[i].x << ", " << displays[i].y << ")\n";
    if (displays.empty())
        std::cerr << "경고: 전광판 마커를 못 찾음 (kDisplayMarker 색상 범위 확인 필요)\n";

    // ── 결과 저장 ──
    std::ostringstream out;
    out << gridToCArray(grid) << "\n";
    out << pointsToCArray(displays, "EvacDisplays", "EVAC_DISPLAY_COUNT") << "\n";
    out << pointsToCArray(exits, "EvacExits", "EVAC_EXIT_COUNT") << "\n";

    std::ofstream cFile("evac_bitmap.c");
    cFile << out.str();
    cFile.close();

    saveColorPreview(grid, displays, exits, "evac_preview.png");
    saveDebugText(grid, displays, exits, "evac_debug.txt");

    std::cout << "\n저장 완료: evac_bitmap.c, evac_preview.png, evac_debug.txt\n";
    std::cout << "main.c에 반영하기 전에 evac_preview.png / evac_debug.txt로 먼저 확인하세요.\n";

    if (argc >= 3)
    {
        if (patchMainC(argv[2], out.str()))
            std::cout << "[main.c 갱신] " << argv[2] << " 안의 EVAC_DATA 구역 자동 교체 완료\n";
    }
    return 0;
}
