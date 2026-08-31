// 액추에이터 mock 구현. 보드 없이 개발 · 시연할 때 쓴다.
// 상태만 들고 있고 UART 전송은 하지 않는다 (CMake USE_MOCK_ACTUATOR).

#include "actuator_control.h"
#include <atomic>
#include <mutex>
#include <iostream>

// 액추에이터 현재 상태 (명세서 actuator_status 값 그대로)
static std::atomic<int> g_fan{0};     // 0=OFF, 1~3=약/중/강
static std::atomic<int> g_valve{1};   // 1=열림(평상시), 0=닫힘
static std::atomic<int> g_siren{0};   // 0=OFF, 1=ON

// 요구사항: 장치별 출처 및 연결 상태 추가
static std::string g_fanSrc = "auto";
static std::string g_valveSrc = "auto";
static std::string g_sirenSrc = "auto";
static std::atomic<bool> g_linkOk{true};

static std::mutex g_mtx;              // 자동(센서 스레드)·수동(수신 스레드) 명령 직렬화

bool Actuator_Init(const char* devPath) {
    (void)devPath;   // mock은 포트 안 씀
    g_linkOk = true; // mock은 항상 연결 성공
    return true;
}

// 명령 실행부. 수동·자동 모두 여기로 수렴. 나중에 STM UART도 이 안에만 추가
bool Actuator_Execute(const std::string& target, const std::string& action,
                      const std::string& src, std::string* reason) {
    std::lock_guard<std::mutex> lock(g_mtx);

    // 출처 판별 로직 ("수동"으로 시작하면 manual, 아니면 auto)
    std::string srcType = (src.find("수동") == 0) ? "manual" : "auto";

    if (target == "fan") {
        if      (action == "off")  g_fan = 0;
        else if (action == "low")  g_fan = 1;
        else if (action == "mid")  g_fan = 2;
        else if (action == "high") g_fan = 3;
        g_fanSrc = srcType;
    }
    else if (target == "valve") {
        g_valve = (action == "open") ? 1 : 0;
        g_valveSrc = srcType;
    }
    else if (target == "siren") {
        g_siren = (action == "on")   ? 1 : 0;
        g_sirenSrc = srcType;
    }
    else {
        if (reason) *reason = "알 수 없는 제어 대상";
        return false;
    }

    std::cout << "[제어][" << src << "] " << target << " action=" << action << "\n";
    return true;
}

// 자동 대응. decideResponse() 결과를 그대로 적용 (사이렌 → 밸브 → 팬 순)
bool Actuator_Apply(const ActuatorCommand& c, const std::string& src) {
    bool ok = true;
    ok &= Actuator_Execute("siren", c.siren ? "on" : "off", src);
    ok &= Actuator_Execute("valve", c.valve ? "open" : "close", src);
    const char* fanAct = (c.fan == 0) ? "off" : (c.fan == 1) ? "low"
                       : (c.fan == 2) ? "mid" : "high";
    ok &= Actuator_Execute("fan", fanAct, src);
    return ok;
}

bool Actuator_Poll() {
    g_linkOk = true; // Mock은 항상 응답함
    return true;
}

ActuatorSnapshot Actuator_GetState() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return { g_fan.load(), g_valve.load(), g_siren.load(),
            g_fanSrc, g_valveSrc, g_sirenSrc, g_linkOk.load(), "" };
}