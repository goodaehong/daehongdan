#pragma once

// 액추에이터 제어 인터페이스. 밸브 · 환기팬 · 사이렌 명령과 상태 조회를 정의한다.
// 실물(actuator_controller.cpp)과 mock 이 같은 인터페이스를 구현한다.

#include <string>
#include <vector>
#include <cstdint>

// 실행할 대응 값. 판단 쪽 Response 를 직접 받지 않고 이 구조체로만 받는다
// (판단 결과 구조가 바뀌어도 액추에이터 코드는 그대로 두려고)
struct ActuatorCommand {
    int fan;     // 0=OFF, 1~3=약/중/강
    int valve;   // 0=닫힘, 1=열림
    int siren;   // 0=OFF, 1=ON
};

// 패킷 특수 바이트
constexpr uint8_t STX = 0x02;
constexpr uint8_t ETX = 0x03;

// 명령어 ID (Command)
constexpr uint8_t CMD_FAN_CTRL   = 0x10;
constexpr uint8_t CMD_VALVE_CTRL = 0x20;
constexpr uint8_t CMD_SIREN_CTRL = 0x30;
constexpr uint8_t CMD_REQ_STATUS = 0x40;
constexpr uint8_t CMD_GAS_EMERG  = 0x50;
constexpr uint8_t CMD_MAX_EMERG  = 0x60;
constexpr uint8_t CMD_SYS_RESET  = 0x70;

// 제어 상태 값
constexpr uint8_t FAN_OFF = 0x00, FAN_LOW = 0x01, FAN_MID = 0x02, FAN_HIGH = 0x03;
constexpr uint8_t VALVE_CLOSED = 0x00, VALVE_OPEN = 0x01;
constexpr uint8_t SIREN_OFF = 0x00, SIREN_ON = 0x01;

struct StmActuatorStatus {
    uint8_t fan;
    uint8_t valve;
    uint8_t siren;
};

// 액추에이터 현재 상태 (명세서 actuator_status 값 그대로)
struct ActuatorSnapshot { 
    int fan, valve, siren;
    std::string fanSrc, valveSrc, sirenSrc;
    bool linkOk; 
    std::string linkReason;
};

// 시작 시 1회. UART 포트 열기. 실패해도 서버는 계속 감
bool Actuator_Init(const char* devPath);

// 자동 대응 실행. 판단이 정한 값을 그대로 받음 (왜 이 값인지는 몰라도 됨)
// src = 로그 출처 표시용 ("자동:gas" 등), 동작엔 영향 없음
bool Actuator_Apply(const ActuatorCommand& c, const std::string& src);

// 수동 제어 (Qt 버튼). target="fan"/"valve"/"siren"
// action: fan="off"/"low"/"mid"/"high", valve="open"/"close", siren="on"/"off"
bool Actuator_Execute(const std::string& target, const std::string& action,
                      const std::string& src, std::string* reason = nullptr);

// 지금 상태 꺼내기 (Qt actuator_status 전송용)
ActuatorSnapshot Actuator_GetState();

// STM에 상태 요청(0x40), linkOk 갱신
bool Actuator_Poll();