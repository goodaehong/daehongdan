/*
 * 라즈베리파이에서 하드코딩된 값(표정/가스/온습도/시각/날짜)을
 * STM32 display_board로 UART 전송하는 테스트 프로그램.
 * 빌드: gcc send_test_uart.c -o send_test_uart
 * 실행: sudo ./send_test_uart   (또는 유저를 dialout 그룹에 추가해서 sudo 없이)
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#define STX 0xAA
#define ETX 0x55
#define CMD_UPDATE 0x80

static int open_uart(const char *devPath)
{
    int fd = open(devPath, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror("open");
        return -1;
    }

    struct termios options;
    tcgetattr(fd, &options);

    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);

    options.c_cflag &= ~PARENB;   /* 패리티 없음 */
    options.c_cflag &= ~CSTOPB;   /* 스톱비트 1 */
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;       /* 데이터비트 8 */
    options.c_cflag |= (CLOCAL | CREAD);

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG); /* raw 모드 */
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &options);
    return fd;
}

/* data: 표정,가스색상,가스H,가스L,온도,습도,시,분,년,월,일 (11바이트, CMD 0x80 기준) */
static int build_packet(uint8_t *out, const uint8_t *data, uint8_t dataLen, uint8_t cmd)
{
    int idx = 0;
    out[idx++] = STX;
    out[idx++] = dataLen;
    out[idx++] = cmd;

    uint8_t checksum = dataLen + cmd;
    for (uint8_t i = 0; i < dataLen; i++)
    {
        out[idx++] = data[i];
        checksum += data[i];
    }

    out[idx++] = checksum;
    out[idx++] = ETX;
    return idx;
}

int main(void)
{
    const char *devPath = "/dev/serial0";
    int fd = open_uart(devPath);
    if (fd < 0)
    {
        return 1;
    }

    /* 하드코딩 테스트 값: 웃음/정상/가스100/온도25/습도50/12:30/2026-07-28 */
    uint8_t data[11] = { 0x00, 0x00, 0x00, 0x64, 25, 50, 12, 30, 26, 7, 28 };
    uint8_t packet[32];

    printf("UART 열림: %s, 1초마다 테스트 패킷 전송 시작 (Ctrl+C로 종료)\n", devPath);

    while (1)
    {
        int len = build_packet(packet, data, sizeof(data), CMD_UPDATE);
        ssize_t written = write(fd, packet, len);
        if (written != len)
        {
            perror("write");
        }
        else
        {
            printf("패킷 전송 완료 (%d bytes)\n", len);
        }
        sleep(1);
    }

    close(fd);
    return 0;
}
