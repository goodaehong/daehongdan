#ifdef _WIN32
#include <windows.h>
#endif
#include <iostream>
#include "EvacPlanner.h"

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   // 윈도우 콘솔이 UTF-8 출력을 CP949로 잘못 읽어서 한글 깨지는 것 방지
#endif
    std::cout << "=== 대피 경로 파이프라인 서버 시작 ===\n";

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

    std::vector<std::vector<Point>> calculatedRoutes = processFloorPlan("map.png");
    std::vector<std::vector<int>> bitmap = getEvacBitmap("map.png");
    std::vector<Point> displays = getEvacDisplays("map.png");
    std::vector<Point> exits = getEvacExits("map.png");

    std::cout << "[전광판 " << displays.size() << "개] ";
    for (const auto& d : displays) std::cout << "(" << d.y << "," << d.x << ") ";
    std::cout << "\n[출구 " << exits.size() << "개] ";
    for (const auto& e : exits) std::cout << "(" << e.y << "," << e.x << ") ";
    std::cout << "\n";

    if (!calculatedRoutes.empty()) {
        std::cout << "완료\n";
    }else{
        std::cout << "실패\n";
    }


    return 0;
}