#include "actuator_control.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <atomic>
#include <mutex>

// ── 전역 상태 변수 ──
static int g_serial_fd = -1;
static std::string g_devPath = "";

static std::atomic<int> g_fan{0};
static std::atomic<int> g_valve{1};
static std::atomic<int> g_siren{0};
static std::mutex g_mtx;

// ── 내부 유틸리티 함수 (static 선언으로 외부 노출 방지) ──
static uint8_t calcChecksum(uint8_t len, uint8_t cmd, const std::vector<uint8_t>& data) {
    uint8_t checksum = len ^ cmd;
    for (uint8_t b : data) {
        checksum ^= b;
    }
    return checksum;
}

static bool sendPacket(uint8_t cmd, const std::vector<uint8_t>& data) {
    if (g_serial_fd < 0) return false;

    uint8_t len = static_cast<uint8_t>(data.size());
    std::vector<uint8_t> packet;
    
    packet.push_back(STX);
    packet.push_back(len);
    packet.push_back(cmd);
    for (uint8_t b : data) {
        packet.push_back(b);
    }
    packet.push_back(calcChecksum(len, cmd, data));
    packet.push_back(ETX);

    ssize_t written = write(g_serial_fd, packet.data(), packet.size());
    return written == static_cast<ssize_t>(packet.size());
}

static bool readByteTimeout(uint8_t& outByte, int timeoutMs) {
    struct pollfd pfd = { .fd = g_serial_fd, .events = POLLIN };
    if (poll(&pfd, 1, timeoutMs) > 0) {
        return read(g_serial_fd, &outByte, 1) == 1;
    }
    return false;
}

static bool readResponse(int timeoutMs, uint8_t& outCmd, StmActuatorStatus& outStatus) {
    if (g_serial_fd < 0) return false;
    uint8_t b;

    do {
        if (!readByteTimeout(b, timeoutMs)) return false;
    } while (b != STX);

    uint8_t len, cmd, checksum, etx;
    if (!readByteTimeout(len, timeoutMs)) return false;
    if (!readByteTimeout(cmd, timeoutMs)) return false;

    std::vector<uint8_t> data(len);
    for (uint8_t i = 0; i < len; i++) {
        if (!readByteTimeout(data[i], timeoutMs)) return false;
    }

    if (!readByteTimeout(checksum, timeoutMs)) return false;
    if (!readByteTimeout(etx, timeoutMs) || etx != ETX) return false;

    if (checksum != calcChecksum(len, cmd, data)) return false;

    outCmd = cmd;
    if (cmd == CMD_REQ_STATUS && len == 3) {
        outStatus.fan = data[0];
        outStatus.valve = data[1];
        outStatus.siren = data[2];
    }
    return true;
}

static bool waitForAck(const std::string& label) {
    uint8_t cmd;
    StmActuatorStatus status;
    if (readResponse(1000, cmd, status)) {
        return true;
    }
    std::cerr << "[" << label << "] 응답 없음 (Timeout)" << std::endl;
    return false;
}

// ── 외부 공개 API 구현부 ──

bool Actuator_Init(const char* devPath) {
    g_devPath = devPath;
    g_serial_fd = open(devPath, O_RDWR | O_NOCTTY);
    if (g_serial_fd < 0) {
        std::cerr << "[오류] UART 열기 실패: " << g_devPath << std::endl;
        return false;
    }

    fcntl(g_serial_fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(g_serial_fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    tcsetattr(g_serial_fd, TCSANOW, &options);
    std::cout << "[Actuator] UART 연결 성공 (" << g_devPath << ")" << std::endl;
    return true;
}

void Actuator_Execute(const std::string& target, const std::string& action, const std::string& src) {
    std::lock_guard<std::mutex> lock(g_mtx);

    // 주의: sendPacket의 두 번째 인자는 std::vector<uint8_t>이므로 {}로 감싸서 전달합니다.
    if (target == "fan") {
        if (action == "off") {
            g_fan = 0;
            sendPacket(CMD_FAN_CTRL, {FAN_OFF});
        }
        else if (action == "low") {
            g_fan = 1;
            sendPacket(CMD_FAN_CTRL, {FAN_LOW});
        }
        else if (action == "mid") {
            g_fan = 2;
            sendPacket(CMD_FAN_CTRL, {FAN_MID});
        }
        else if (action == "high") {
            g_fan = 3;
            sendPacket(CMD_FAN_CTRL, {FAN_HIGH});
        }
        waitForAck("FanControl");
    }
    else if (target == "valve") {
        if(action == "open") {
            g_valve = 1;
            sendPacket(CMD_VALVE_CTRL, {VALVE_OPEN});
        }
        else if (action == "close") {
            g_valve = 0;
            sendPacket(CMD_VALVE_CTRL, {VALVE_CLOSED});
        }
        waitForAck("ValveControl");
    }
    else if (target == "siren") {
        if(action == "on") {
            g_siren = 1;
            sendPacket(CMD_SIREN_CTRL, {SIREN_ON});
        }
        else if (action == "off") {
            g_siren = 0;
            sendPacket(CMD_SIREN_CTRL, {SIREN_OFF});
        }
        waitForAck("SirenControl");
    }
    else if (target == "gas_emergency") {
        sendPacket(CMD_GAS_EMERG, {});
        waitForAck("GasEmergency");
    }
    else if (target == "max_emergency") {
        sendPacket(CMD_MAX_EMERG, {});
        waitForAck("MaxEmergency");
    }
    else if (target == "system_reset") {
        sendPacket(CMD_SYS_RESET, {});
        waitForAck("SystemReset");
    }
    else return;

    std::cout << "[제어][" << src << "] " << target << " action=" << action << "\n";
}

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