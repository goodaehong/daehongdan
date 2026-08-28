// test_alert_uart.cpp의 화재 테스트(3~8번 메뉴)는 (5,5)/(40,20) 같은 하드코딩된
// 좌표를 그냥 UART로 쏘기만 한다. 이 도구는 그 대신 실제 카메라 영상을
// FireDetectionRuntime(server/detection, ArUco 기반 격자 변환)에 그대로 태워서 나온
// 진짜 gridX/gridY/displayRadiusCells를 STM32로 전송한다 - 카메라 → 화재 검출 →
// 격자 변환 → UART 전송까지 실제 파이프라인이 맞물려 동작하는지 확인하는 용도.
//
// server_main.cpp의 화재 유지시간 디바운스(FIRE_POS_HOLD_SEC)는 재현하지 않는다.
// 감지 결과가 바뀌면 그 즉시 보낸다 - 카메라 앞에서 불(라이터/촛불 등)을 움직였을 때
// 화면이 바로 반응하는지 눈으로 확인하기 쉽게 하기 위함이며, 그만큼 프로덕션보다
// 더 자주/민감하게 전송된다는 점을 감안할 것.
//
// 사용법:
//   ./fire_uart_test <카메라 인덱스|영상 경로> --aruco-config <설정파일>
//                     [--channel 1-4] [--dev /dev/stm_display] [--max-frames N]

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp>

#include "FireDetectionRuntime.h"

extern "C" {
#include "stm_display_protocol.h"
}

namespace {

struct FireCell {
    int x = 0, y = 0, radius = 0;
};

bool sameFires(const std::vector<FireCell>& a, const std::vector<FireCell>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].radius != b[i].radius) return false;
    return true;
}

// gridPositionValid인 박스만, 최대 STM_DISPLAY_EVAC_MAX_FIRES개까지 모은다
// (server_main.cpp의 collectFires()/sendEvacPaths()와 같은 상한 규칙)
std::vector<FireCell> collectFires(const DetectionResult& detection) {
    std::vector<FireCell> fires;
    for (const auto& box : detection.boxes) {
        if (!box.gridPositionValid) continue;
        if ((int)fires.size() >= STM_DISPLAY_EVAC_MAX_FIRES) {
            std::cerr << "[fire_uart_test] 화재가 " << detection.boxes.size()
                      << "곳 감지 - 최대 " << STM_DISPLAY_EVAC_MAX_FIRES << "곳까지만 전송\n";
            break;
        }
        fires.push_back({ box.gridX, box.gridY, box.displayRadiusCells });
    }
    return fires;
}

bool sendFires(int fd, const std::vector<FireCell>& fires) {
    std::vector<uint8_t> xyr;
    xyr.reserve(fires.size() * 3);
    for (const auto& f : fires) {
        xyr.push_back((uint8_t)f.x);
        xyr.push_back((uint8_t)f.y);
        xyr.push_back((uint8_t)f.radius);
    }
    const bool ok = StmDisplayProtocol_SendEvacFires(
        fd, fires.empty() ? nullptr : xyr.data(), (uint8_t)fires.size());

    std::cout << "[fire_uart_test] 화재 " << fires.size() << "곳 전송 "
               << (ok ? "성공" : "실패");
    for (const auto& f : fires)
        std::cout << " (" << f.x << "," << f.y << ")r" << f.radius;
    std::cout << "\n";
    return ok;
}

bool isCameraIndex(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(),
        [](unsigned char c) { return std::isdigit(c) != 0; });
}

}  // namespace

int main(int argc, char** argv) {
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    if (argc < 2) {
        std::cerr << "사용법: " << argv[0]
                  << " <카메라 인덱스|영상 경로> --aruco-config <설정파일> "
                     "[--channel 1-4] [--dev /dev/stm_display] [--max-frames N]\n";
        return 1;
    }

    const std::string source = argv[1];
    std::string arucoConfigPath;
    std::string devPath = "/dev/stm_display";
    std::size_t channel = 1;
    std::uint64_t maxFrames = 0;

    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--aruco-config" && i + 1 < argc) arucoConfigPath = argv[++i];
        else if (a == "--channel" && i + 1 < argc) channel = (std::size_t)std::stoi(argv[++i]);
        else if (a == "--dev" && i + 1 < argc) devPath = argv[++i];
        else if (a == "--max-frames" && i + 1 < argc) maxFrames = std::stoull(argv[++i]);
    }

    if (arucoConfigPath.empty()) {
        std::cerr << "--aruco-config 없이는 격자 좌표를 계산할 수 없어서 실행 못 함\n";
        return 2;
    }

    cv::VideoCapture capture;
    if (isCameraIndex(source)) {
#if defined(_WIN32)
        capture.open(std::stoi(source), cv::CAP_DSHOW);
#else
        capture.open(std::stoi(source));
#endif
    } else {
        capture.open(source);
    }
    if (!capture.isOpened()) {
        std::cerr << "영상 소스를 못 열었음: " << source << "\n";
        return 1;
    }

    FireDetectionRuntime runtime;
    if (!runtime.loadArucoBoardConfiguration(arucoConfigPath, channel - 1)) {
        std::cerr << "[ARUCO] 설정 로드 실패: " << runtime.arucoMappingError() << "\n";
        return 2;
    }
    std::cout << "[ARUCO] 채널 " << channel << " 설정 로드 완료 (" << arucoConfigPath << ")\n";

    int fd = StmDisplayProtocol_Open(devPath.c_str());
    if (fd < 0) {
        std::cerr << "UART 열기 실패 (" << devPath << ")\n";
        return 1;
    }

    std::vector<FireCell> lastSent;
    std::uint64_t frameId = 0;
    cv::Mat frame;

    std::cout << "[fire_uart_test] 시작 - Ctrl+C로 종료\n";
    while (capture.read(frame) && !frame.empty()) {
        ++frameId;
        const auto now = FireDetectionRuntime::Clock::now();
        runtime.submitFrame(frame, frameId, now);
        FireRuntimeSnapshot snap = runtime.poll(now);

        // ArUco 마커가 안 보이거나 homography가 아직 안 맞으면 gridPositionValid가
        // 계속 false라 아무것도 안 보내진다 - 60프레임마다 상태를 찍어서 원인 구분
        if (snap.resultIsFresh && frameId % 60 == 0) {
            const ArucoMappingStatus& a = snap.arucoMapping;
            std::cout << "[ARUCO 상태] configured=" << a.configured
                      << " valid=" << a.homographyValid
                      << " fresh=" << a.homographyFresh
                      << " markers=" << a.acceptedMarkers << "/" << a.detectedMarkers
                      << " msg=\"" << a.message << "\"\n";
        }

        if (snap.resultIsFresh) {
            const std::vector<FireCell> fires = collectFires(snap.detection);
            if (!sameFires(fires, lastSent)) {
                if (!sendFires(fd, fires))
                    fd = StmDisplayProtocol_Reconnect(fd, devPath.c_str());
                else
                    lastSent = fires;
            }
        }

        if (maxFrames > 0 && frameId >= maxFrames) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    StmDisplayProtocol_Close(fd);
    runtime.stop();
    return 0;
}
