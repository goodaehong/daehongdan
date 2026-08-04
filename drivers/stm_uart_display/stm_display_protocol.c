#include "stm_display_protocol.h"

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#define STM_DISPLAY_STX 0xAA
#define STM_DISPLAY_ETX 0x55

/* STX+LEN+CMD+DATA...+체크섬+ETX 프레임 조립해서 그대로 전송.
   체크섬 = (LEN + CMD + 모든 DATA바이트)의 8비트 합산 - STM32 파서와 동일한 계산식이라
   여기서 한 비트라도 다르게 계산하면 STM32가 매 패킷마다 체크섬 불일치로 버림 */
static bool SendPacket(int fd, uint8_t cmd, const uint8_t *data, uint8_t dataLen)
{
    uint8_t packet[32];
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

void StmDisplayProtocol_Close(int fd)
{
    if (fd >= 0)
    {
        close(fd);
    }
}
