#ifndef ACTUATOR_CONTROLLER_H
#define ACTUATOR_CONTROLLER_H

#include <string>
#include <vector>
#include <cstdint>

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

// 상태 응답 구조체
struct StmActuatorStatus {
    uint8_t fan;
    uint8_t valve;
    uint8_t siren;
};

class ActuatorController {
private:
    int fd;
    std::string devPath;

    // 내부 통신용 유틸리티 함수 (기존 .c 파일의 로직을 대체)
    uint8_t calcChecksum(uint8_t len, uint8_t cmd, const std::vector<uint8_t>& data);
    bool sendPacket(uint8_t cmd, const std::vector<uint8_t>& data);
    bool readByteTimeout(uint8_t& outByte, int timeoutMs);
    bool readResponse(int timeoutMs, uint8_t& outCmd, StmActuatorStatus& outStatus);
    bool waitForAck(const std::string& label);

public:
    ActuatorController(const std::string& path);
    ~ActuatorController();

    bool init(); // UART 통신 초기화

    // 개별 제어
    bool controlFan(uint8_t speed);
    bool controlValve(uint8_t state);
    bool controlSiren(uint8_t state);

    // 특수 제어
    bool triggerGasEmergency();
    bool triggerMaxEmergency();
    bool systemReset();

    // 상태 확인
    bool printStatus();
};

#endif // ACTUATOR_CONTROLLER_H