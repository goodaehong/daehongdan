#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <iostream>
#include "EvacPlanner.h"

// 사용법: ./evac_server [이미지경로] [main.c 경로] [--fires x,y,r x,y,r ...]
//   이미지경로 생략 시 "map.png" 사용
//   main.c 경로를 주면, 같은 이미지 분석 결과로 STM32 main.c의 EVAC_DATA 구역까지 자동 반영
//   --fires 뒤에 "x,y,r" 형태로 화재를 몇 개든 나열하면, processFloorPlan()이 그 화재를
//   피해가는 경로를 계산함(다익스트라 화재 우회) - 안 주면 기존처럼 화재 없이 계산
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // 윈도우 콘솔이 UTF-8 출력을 CP949로 잘못 읽어서 한글 깨지는 것 방지
#endif
    std::vector<std::string> positional;
    std::vector<EvacFireCell> fires;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--fires") {
            for (int j = i + 1; j < argc; j++) {
                int x, y, r;
                if (std::sscanf(argv[j], "%d,%d,%d", &x, &y, &r) != 3) break;
                fires.push_back({x, y, r});
                i = j;
            }
        } else {
            positional.push_back(arg);
        }
    }
    std::string floorPlanImage = positional.size() >= 1 ? positional[0] : "map.png";
    std::string mainCPath = positional.size() >= 2 ? positional[1] : "";

    std::cout << "=== 대피 경로 파이프라인 서버 시작 ===\n";
    if (!fires.empty()) {
        std::cout << "[화재 우회 테스트] 화재 " << fires.size() << "곳: ";
        for (const auto& f : fires) std::cout << "(" << f.x << "," << f.y << ")r" << f.radius << " ";
        std::cout << "\n";
    }

    // 1. 단일 함수 호출로 파이프라인 전체 실행
    // (이미지 읽기 -> 비트맵/디버그맵/PNG 저장 -> 경로 계산 -> 경로 텍스트 저장 -> 경로 리턴)
    // std::string floorPlanImage = "map.png"; // 입력할 평면도 사진 파일
    
    //std::vector<Route> calculatedRoutes = processFloorPlan(floorPlanImage);

    // // 2. 리턴된 데이터를 서버 로직에서 활용
    // if (!calculatedRoutes.empty()) {
    //     std::cout << "[서버 확인] 총 " << calculatedRoutes.size() << "개의 경로 세트가 반환되었습니다.\n";
        
    //     // 예시: 리턴된 데이터를 서버 콘솔에 간단히 출력
    //     for (const auto& route : calculatedRoutes) {
    //         std::cout << "Start ID " << route.start_id << " -> Exit ID " << route.exit_id 
    //                   << " (길이: " << route.path.size() << ")\n";
    //     }
    // } else {
    //     std::cout << "[오류] 경로를 생성하지 못했습니다. 이미지를 확인하세요.\n";
    // }

    std::vector<std::vector<Point>> calculatedRoutes = processFloorPlan(floorPlanImage, fires);
    std::vector<std::vector<int>> bitmap = getEvacBitmap(floorPlanImage);
    std::vector<Point> displays = getEvacDisplays(floorPlanImage);
    std::vector<Point> exits = getEvacExits(floorPlanImage);

    std::cout << "[전광판 " << displays.size() << "개] ";
    for (const auto& d : displays) std::cout << "(" << d.y << "," << d.x << ") ";
    std::cout << "\n[출구 " << exits.size() << "개] ";
    for (const auto& e : exits) std::cout << "(" << e.y << "," << e.x << ") ";
    std::cout << "\n";

    if (!calculatedRoutes.empty()) {
        std::cout << "완료 (" << calculatedRoutes.size() << "개 경로 반환됨)\n";
        for (size_t i = 0; i < calculatedRoutes.size(); i++) {
            std::cout << "  route[" << i << "] (" << calculatedRoutes[i].size() << "칸): ";
            for (const auto& p : calculatedRoutes[i]) std::cout << "(" << p.y << "," << p.x << ") ";
            std::cout << "\n";
        }
    }else{
        std::cout << "실패\n";
    }

    if (!mainCPath.empty()) {
        exportToMainC(floorPlanImage, mainCPath);   // 실패 시 exportToMainC 내부에서 이미 에러 로그를 찍음
    }

    return 0;
}