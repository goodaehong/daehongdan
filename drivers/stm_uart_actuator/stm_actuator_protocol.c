#include "stm_actuator_protocol.h"
 
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <string.h>

/* 체크섬: Command + Data 바이트를 모두 XOR (Length는 미포함 - 명세 문구 그대로.
   실제 STM32 파싱 코드와 다르면 여기 한 군데만 고치면 됨) */
static uint8_t calc_checksum(uint8_t cmd, const uint8_t *data, uint8_t dataLen)
{
    uint8_t checksum = cmd;
    for (uint8_t i = 0; i < dataLen; i++)
    {
        checksum ^= data[i];
    }
    return checksum;
}

static bool send_packet(int fd, uint8_t cmd, const uint8_t *data, uint8_t dataLen)
{
    if (fd < 0)
    {
        return false;   /* STM32 연결 안 됐어도 호출부가 죽지 않게 조용히 실패 */
    }
 
    uint8_t packet[16];
    int idx = 0;
 
    packet[idx++] = STM_ACTUATOR_STX; // [0x02]
    packet[idx++] = dataLen; // [데이터 길이]
    packet[idx++] = cmd; // [명령어]
 
    for (uint8_t i = 0; i < dataLen; i++)
    {
        packet[idx++] = data[i]; // [데이터 바이트들]
    }
 
    packet[idx++] = calc_checksum(cmd, data, dataLen); // [체크섬]
    packet[idx++] = STM_ACTUATOR_ETX; // [0x03]
 
    ssize_t written = write(fd, packet, idx); // UART로 실제 전송
    return written == idx;
}

int StmActuator_Open(const char *devPath)
{
    int fd = open(devPath, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        return -1;
    }
 
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

void StmActuator_Close(int fd)
{
    if (fd >= 0)
    {
        close(fd);
    }
}

bool StmActuator_SendFan(int fd, uint8_t fanSpeed)
{
    uint8_t data[1] = { fanSpeed };
    return send_packet(fd, CMD_FAN_CTRL, data, sizeof(data));
}
 
bool StmActuator_SendValve(int fd, uint8_t valveState)
{
    uint8_t data[1] = { valveState };
    return send_packet(fd, CMD_VALVE_CTRL, data, sizeof(data));
}
 
bool StmActuator_SendSiren(int fd, uint8_t sirenState)
{
    uint8_t data[1] = { sirenState };
    return send_packet(fd, CMD_SIREN_CTRL, data, sizeof(data));
}
 
bool StmActuator_SendReqStatus(int fd)
{
    return send_packet(fd, CMD_REQ_STATUS, NULL, 0);
}
 
bool StmActuator_SendGasEmerg(int fd)
{
    return send_packet(fd, CMD_GAS_EMERG, NULL, 0);
}
 
bool StmActuator_SendMaxEmerg(int fd)
{
    return send_packet(fd, CMD_MAX_EMERG, NULL, 0);
}
 
bool StmActuator_SendSysReset(int fd)
{
    return send_packet(fd, CMD_SYS_RESET, NULL, 0);
}

/* 바이트 하나를 timeoutMs 안에 읽음. 실패 시 false */
static bool read_byte_timeout(int fd, uint8_t *out, int timeoutMs)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int ret = poll(&pfd, 1, timeoutMs);
    if (ret <= 0)
    {
        return false;   /* timeout 또는 에러 */
    }
    ssize_t n = read(fd, out, 1);
    return n == 1;
}

bool StmActuator_ReadResponse(int fd, int timeoutMs, uint8_t *outCmd, StmActuatorStatus *outStatus)
{
    if (fd < 0)
    {
        return false;
    }
 
    uint8_t b;
 
    /* STX 나올 때까지 동기화 (잡음 바이트 버림) */
    do
    {
        if (!read_byte_timeout(fd, &b, timeoutMs))
        {
            return false;
        }
    } while (b != STM_ACTUATOR_STX);
 
    uint8_t len;
    if (!read_byte_timeout(fd, &len, timeoutMs))
    {
        return false;
    }
 
    uint8_t cmd;
    if (!read_byte_timeout(fd, &cmd, timeoutMs))
    {
        return false;
    }
 
    uint8_t data[8] = {0};
    if (len > sizeof(data))
    {
        return false;   /* 비정상적으로 큰 길이 - 프레임 깨짐으로 간주 */
    }
    for (uint8_t i = 0; i < len; i++)
    {
        if (!read_byte_timeout(fd, &data[i], timeoutMs))
        {
            return false;
        }
    }
 
    uint8_t checksum;
    if (!read_byte_timeout(fd, &checksum, timeoutMs))
    {
        return false;
    }
 
    uint8_t etx;
    if (!read_byte_timeout(fd, &etx, timeoutMs))
    {
        return false;
    }
    if (etx != STM_ACTUATOR_ETX)
    {
        return false;   /* 프레임 깨짐 */
    }
 
    uint8_t expected = calc_checksum(cmd, data, len);
    if (checksum != expected)
    {
        return false;   /* 체크섬 불일치 */
    }
 
    if (outCmd)
    {
        *outCmd = cmd;
    }
 
    if (cmd == CMD_REQ_STATUS && len == 3 && outStatus)
    {
        outStatus->fan   = data[0];
        outStatus->valve = data[1];
        outStatus->siren = data[2];
    }
 
    return true;
}