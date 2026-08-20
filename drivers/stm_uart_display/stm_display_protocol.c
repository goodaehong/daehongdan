#include "stm_display_protocol.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <string.h>

#define STM_DISPLAY_STX 0xAA
#define STM_DISPLAY_ETX 0x55

/* STX+LEN+CMD+DATA...+체크섬+ETX 프레임 조립해서 그대로 전송.
   체크섬 = (LEN + CMD + 모든 DATA바이트)의 8비트 합산 - STM32 파서와 동일한 계산식이라
   여기서 한 비트라도 다르게 계산하면 STM32가 매 패킷마다 체크섬 불일치로 버림 */
static bool SendPacket(int fd, uint8_t cmd, const uint8_t *data, uint8_t dataLen)
{
    uint8_t packet[70];   /* CMD_EVAC_PATH가 최대 64바이트 데이터를 쓰므로 32에서 늘림 */
    int idx = 0;

    packet[idx++] = STM_DISPLAY_STX;
    packet[idx++] = dataLen;
    packet[idx++] = cmd;

    uint8_t checksum = dataLen + cmd;
    for (uint8_t i = 0; i < dataLen; i++)
    {
        packet[idx++] = data[i];
        checksum += data[i];
    }

    packet[idx++] = checksum;
    packet[idx++] = STM_DISPLAY_ETX;

    ssize_t written = write(fd, packet, idx);
    return written == idx;
}

int StmDisplayProtocol_Open(const char *devPath)
{
    int fd = open(devPath, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        return -1;
    }

    /* send_test_uart.c에서 검증됐던 것과 동일한 설정 (115200 8N1, raw 모드) */
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

int StmDisplayProtocol_Reconnect(int oldFd, const char *devPath)
{
    StmDisplayProtocol_Close(oldFd);   /* oldFd<0이면 내부에서 그냥 무시함 */
    return StmDisplayProtocol_Open(devPath);
}

bool StmDisplayProtocol_SendUpdate(int fd,
                                    uint8_t face, uint8_t gasColor, uint16_t gas,
                                    uint8_t temp, uint8_t humidity,
                                    uint8_t hour, uint8_t minute,
                                    uint8_t year, uint8_t month, uint8_t day)
{
    if (fd < 0)
    {
        return false;   /* STM32 연결 안 됐어도 호출한 쪽이 죽지 않게 조용히 실패 처리 */
    }

    /* 순서는 main.c의 UPDATE_DATA_LEN(11) 파싱 순서와 정확히 일치해야 함 */
    uint8_t data[11] = {
        face, gasColor,
        (uint8_t)((gas >> 8) & 0xFF), (uint8_t)(gas & 0xFF),
        temp, humidity,
        hour, minute,
        year, month, day
    };

    return SendPacket(fd, STM_DISPLAY_CMD_UPDATE, data, sizeof(data));
}

bool StmDisplayProtocol_SendAlert(int fd, uint8_t disasterType, uint8_t zoneId)
{
    if (fd < 0)
    {
        return false;
    }

    uint8_t data[2] = { disasterType, zoneId };
    return SendPacket(fd, STM_DISPLAY_CMD_ALERT, data, sizeof(data));
}

bool StmDisplayProtocol_SendClear(int fd)
{
    if (fd < 0)
    {
        return false;
    }

    return SendPacket(fd, STM_DISPLAY_CMD_CLEAR, NULL, 0);
}

bool StmDisplayProtocol_SendEvacPath(int fd, uint8_t routeIndex,
                                      const uint8_t *waypointsXY, uint8_t waypointCount)
{
    if (fd < 0)
    {
        return false;
    }
    if (waypointCount > STM_DISPLAY_EVAC_MAX_WAYPOINTS)
    {
        return false;   /* STM32 packetData 버퍼를 넘어가는 크기는 애초에 안 보냄 */
    }

    /* data[0..1] = routeIndex,waypointCount, 그 뒤로 {x,y} 반복
       - main.c HandlePacket()의 CMD_EVAC_PATH 파싱 순서와 정확히 일치해야 함 */
    uint8_t data[2 + STM_DISPLAY_EVAC_MAX_WAYPOINTS * 2];
    data[0] = routeIndex;
    data[1] = waypointCount;
    memcpy(&data[2], waypointsXY, (size_t)waypointCount * 2);

    return SendPacket(fd, STM_DISPLAY_CMD_EVAC_PATH, data, (uint8_t)(2 + waypointCount * 2));
}

bool StmDisplayProtocol_SendEvacFires(int fd, const uint8_t *firesXYR, uint8_t fireCount)
{
    if (fd < 0)
    {
        return false;
    }
    if (fireCount > STM_DISPLAY_EVAC_MAX_FIRES)
    {
        return false;
    }

    /* data[0] = fireCount, 그 뒤로 {x,y,radius} 반복
       - main.c HandlePacket()의 CMD_EVAC_FIRES 파싱 순서와 정확히 일치해야 함 */
    uint8_t data[1 + STM_DISPLAY_EVAC_MAX_FIRES * 3];
    data[0] = fireCount;
    if (fireCount > 0)
    {
        memcpy(&data[1], firesXYR, (size_t)fireCount * 3);
    }

    return SendPacket(fd, STM_DISPLAY_CMD_EVAC_FIRES, data, (uint8_t)(1 + fireCount * 3));
}

/* poll()로 timeoutMs 안에 데이터 들어오길 기다렸다가 1바이트 읽음. 못 받으면 false */
static bool ReadByteTimeout(int fd, uint8_t *outByte, int timeoutMs)
{
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    if (poll(&pfd, 1, timeoutMs) > 0)
    {
        return read(fd, outByte, 1) == 1;
    }
    return false;
}

bool StmDisplayProtocol_ReadAck(int fd, int timeoutMs, uint8_t *outStatus)
{
    if (fd < 0)
    {
        return false;
    }

    uint8_t b;
    do
    {
        if (!ReadByteTimeout(fd, &b, timeoutMs))
        {
            return false;
        }
    } while (b != STM_DISPLAY_STX);

    uint8_t len, cmd;
    if (!ReadByteTimeout(fd, &len, timeoutMs)) return false;
    if (!ReadByteTimeout(fd, &cmd, timeoutMs)) return false;

    uint8_t data[16];
    if (len > sizeof(data))
    {
        return false;   /* 손상된 길이 값 - 정상 ACK(1바이트)일 리 없음 */
    }

    uint8_t checksumCalc = (uint8_t)(len + cmd);
    for (uint8_t i = 0; i < len; i++)
    {
        if (!ReadByteTimeout(fd, &data[i], timeoutMs))
        {
            return false;
        }
        checksumCalc += data[i];
    }

    uint8_t checksum, etx;
    if (!ReadByteTimeout(fd, &checksum, timeoutMs)) return false;
    if (!ReadByteTimeout(fd, &etx, timeoutMs)) return false;

    if (etx != STM_DISPLAY_ETX || checksum != checksumCalc || cmd != STM_DISPLAY_CMD_ACK)
    {
        return false;
    }

    if (outStatus != NULL && len >= 1)
    {
        *outStatus = data[0];
    }
    return true;
}

void StmDisplayProtocol_Close(int fd)
{
    if (fd >= 0)
    {
        close(fd);
    }
}
