#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "../judgement.h"

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
};

// 시작 시 1회. UART 포트 열기. 실패해도 서버는 계속 감
bool Actuator_Init(const char* devPath);

// 자동 대응 실행. decideResponse() 결과 그대로 받음 (왜 이 값인지는 몰라도 됨)
// src = 로그 출처 표시용 ("자동:gas" 등), 동작엔 영향 없음
void Actuator_Apply(const Response& r, const std::string& src);

// 수동 제어 (Qt 버튼). target="fan"/"valve"/"siren"
// action: fan="off"/"low"/"mid"/"high", valve="open"/"close", siren="on"/"off"
void Actuator_Execute(const std::string& target, const std::string& action,
                      const std::string& src, std::string* reason = nullptr);

// 지금 상태 꺼내기 (Qt actuator_status 전송용)
ActuatorSnapshot Actuator_GetState();

// STM에 상태 요청(0x40), linkOk 갱신
bool Actuator_Poll();