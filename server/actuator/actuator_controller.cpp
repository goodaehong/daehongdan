#include "actuator_control.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <atomic>
#include <mutex>
#include <cstring>

// ── 전역 상태 변수 ──
static int g_serial_fd = -1;
static std::string g_devPath = "";

static std::atomic<int> g_fan{0};
static std::atomic<int> g_valve{1};
static std::atomic<int> g_siren{0};

static std::string g_fanSrc = "auto";
static std::string g_valveSrc = "auto";
static std::string g_sirenSrc = "auto";
static std::atomic<bool> g_linkOk{false};
static std::string g_linkReason = "UART 열려있지 않음";

static std::mutex g_mtx;

// UART 포트를 열고 통신 설정을 건다. 성공하면 fd, 실패하면 -1            
// Actuator_Init 과 재연결이 같은 설정을 써야 해서 함수로 뺐다
static int openPort(const char* devPath) {
    int fd = open(devPath, O_RDWR | O_NOCTTY);
    if (fd < 0) return -1;

    fcntl(fd, F_SETFL, 0);

    struct termios options;
    tcgetattr(fd, &options);
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

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}                                                                          

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
    std::vector<uint8_t> packet ={STX, len, cmd};
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

static bool waitForAck(const std::string& label, std::string* reason = nullptr) {
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
    g_serial_fd = openPort(devPath);
    if (g_serial_fd < 0) {
        std::cerr << "[오류] UART 열기 실패: " << g_devPath
                  << " — 보드를 꽂으면 자동으로 다시 붙습니다\n";
        g_linkOk = false;
        g_linkReason = "UART 열려있지 않음";
        return false;
    }
    g_linkOk = true;
    g_linkReason = "";
    std::cout << "[Actuator] UART 연결 성공 (" << g_devPath << ")" << std::endl;
    return true;
}                                                                       

bool Actuator_Execute(const std::string& target, const std::string& action, const std::string& src, std::string* reason) {
    std::lock_guard<std::mutex> lock(g_mtx);
    if (g_serial_fd < 0) {
        if (reason) *reason = "UART 열려있지 않음";
        return false;
    }

    std::string srcType = (src.find("수동") == 0) ? "manual" : "auto";
    bool success = false;

    if (target == "fan") {
        uint8_t val = (action == "off") ? FAN_OFF : (action == "low") ? FAN_LOW : 
                      (action == "mid") ? FAN_MID : FAN_HIGH;
        if (!sendPacket(CMD_FAN_CTRL, {val})) { if (reason) *reason = "UART 쓰기 실패"; } 
        else { success = waitForAck("FanControl", reason); if (success) { g_fan = val; g_fanSrc = srcType; } }
    }
    else if (target == "valve") {
        uint8_t val = (action == "open") ? VALVE_OPEN : VALVE_CLOSED;
        if (!sendPacket(CMD_VALVE_CTRL, {val})) { if (reason) *reason = "UART 쓰기 실패"; } 
        else { success = waitForAck("ValveControl", reason); if (success) { g_valve = val; g_valveSrc = srcType; } }
    }
    else if (target == "siren") {
        uint8_t val = (action == "on") ? SIREN_ON : SIREN_OFF;
        if (!sendPacket(CMD_SIREN_CTRL, {val})) { if (reason) *reason = "UART 쓰기 실패"; } 
        else { success = waitForAck("SirenControl", reason); if (success) { g_siren = val; g_sirenSrc = srcType; } }
    }
    else if (target == "gas_emergency" || target == "max_emergency" || target == "system_reset") {
        uint8_t cmd = (target == "gas_emergency") ? CMD_GAS_EMERG : (target == "max_emergency") ? CMD_MAX_EMERG : CMD_SYS_RESET;
        if (!sendPacket(cmd, {})) { if (reason) *reason = "UART 쓰기 실패"; } 
        else { success = waitForAck(target, reason); }
    }
    else {
        if (reason) *reason = "알 수 없는 제어 대상";
        return false;
    }

    if (success) std::cout << "[제어][" << src << "] " << target << " action=" << action << "\n";
    return success;
}

bool Actuator_Apply(const Response& r, const std::string& src) {
    bool ok = true;
    ok &= Actuator_Execute("siren", r.siren ? "on" : "off", src, nullptr);
    ok &= Actuator_Execute("valve", r.valve ? "open" : "close", src, nullptr);
    const char* fanAct = (r.fan == 0) ? "off" : (r.fan == 1) ? "low" : (r.fan == 2) ? "mid" : "high";
    ok &= Actuator_Execute("fan", fanAct, src, nullptr);
    return ok;
}

bool Actuator_Poll() {                                                  
    std::lock_guard<std::mutex> lock(g_mtx);

    // 끊겨 있으면 다시 열어본다. 보드를 뽑았다 꽂거나, 서버가 먼저 뜬 경우
    // 재시작 없이 붙게 하려는 것 (5초 주기로 호출되므로 그 안에 복구)
    if (g_serial_fd < 0) {
        g_serial_fd = openPort(g_devPath.c_str());
        if (g_serial_fd < 0) {
            g_linkOk = false;
            g_linkReason = "UART 열려있지 않음";
            return false;
        }
        std::cout << "[Actuator] UART 재연결됨 (" << g_devPath << ")\n";
    }

    // 여기서 실패하면 fd 는 살아 있는데 통신만 죽은 상태일 수 있다
    // (케이블만 뽑힌 경우). 닫아둬야 다음 호출에서 다시 연다
    if (!sendPacket(CMD_REQ_STATUS, {})) {
        ::close(g_serial_fd);
        g_serial_fd = -1;
        g_linkOk = false;
        g_linkReason = "STM 응답 없음";
        return false;
    }

    uint8_t cmd;
    StmActuatorStatus status;
    if (readResponse(1000, cmd, status) && cmd == CMD_REQ_STATUS) {
        g_linkOk = true;
        g_linkReason = "";
        return true;
    }

    ::close(g_serial_fd);
    g_serial_fd = -1;
    g_linkOk = false;
    g_linkReason = "STM 응답 없음";
    return false;
}                                                                     

ActuatorSnapshot Actuator_GetState() {
    std::lock_guard<std::mutex> lock(g_mtx);
    return { g_fan.load(), g_valve.load(), g_siren.load(),
             g_fanSrc, g_valveSrc, g_sirenSrc, g_linkOk.load(), g_linkReason };
}