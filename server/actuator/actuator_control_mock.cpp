#include "actuator_control.h"
#include <atomic>
#include <mutex>
#include <iostream>

// 액추에이터 현재 상태 (명세서 actuator_status 값 그대로)
static std::atomic<int> g_fan{0};     // 0=OFF, 1~3=약/중/강
static std::atomic<int> g_valve{1};   // 1=열림(평상시), 0=닫힘
static std::atomic<int> g_siren{0};   // 0=OFF, 1=ON
static std::mutex g_mtx;              // 자동(센서 스레드)·수동(수신 스레드) 명령 직렬화

bool Actuator_Init(const char* devPath) {
    (void)devPath;   // mock은 포트 안 씀
    return true;
}

// 명령 실행부. 수동·자동 모두 여기로 수렴. 나중에 STM UART도 이 안에만 추가
void Actuator_Execute(const std::string& target, const std::string& action,
                      const std::string& src) {
    std::lock_guard<std::mutex> lock(g_mtx);

    if (target == "fan") {
        if      (action == "off")  g_fan = 0;
        else if (action == "low")  g_fan = 1;
        else if (action == "mid")  g_fan = 2;
        else if (action == "high") g_fan = 3;
    }
    else if (target == "valve") g_valve = (action == "open") ? 1 : 0;
    else if (target == "siren") g_siren = (action == "on")   ? 1 : 0;
    else return;   // 모르는 대상은 무시

    std::cout << "[제어][" << src << "] " << target << " action=" << action << "\n";
    // TODO(STM 연결 후): 여기서 UART 패킷 전송 + ACK 수신
}

// 자동 대응. decideResponse() 결과를 그대로 적용 (사이렌 → 밸브 → 팬 순)
void Actuator_Apply(const Response& r, const std::string& src) {
    Actuator_Execute("siren", r.siren ? "on" : "off", src);
    Actuator_Execute("valve", r.valve ? "open" : "close", src);
    const char* fanAct = (r.fan == 0) ? "off" : (r.fan == 1) ? "low"
                       : (r.fan == 2) ? "mid" : "high";
    Actuator_Execute("fan", fanAct, src);
}

ActuatorSnapshot Actuator_GetState() {
    return { g_fan.load(), g_valve.load(), g_siren.load() };
}