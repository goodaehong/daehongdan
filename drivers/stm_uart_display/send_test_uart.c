/*
 * 라즈베리파이에서 실제 시간(년월일시분) + 그럴듯하게 흔들리는 온습도/가스 값을
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
#include <time.h>
#include <stdlib.h>

#define STX 0xAA
#define ETX 0x55
#define CMD_UPDATE 0x80

#define GAS_CAUTION_THRESHOLD 201
#define GAS_DANGER_THRESHOLD  2001

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

static int clamp_int(int value, int min, int max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

/* 이전 값에서 +-range 이내로 랜덤하게 움직이되 [min,max] 범위를 벗어나지 않게 함 (자연스러운 변동) */
static int random_walk(int current, int step, int min, int max)
{
    int delta = (rand() % (2 * step + 1)) - step;
    return clamp_int(current + delta, min, max);
}

int main(void)
{
    const char *devPath = "/dev/serial0";
    int fd = open_uart(devPath);
    if (fd < 0)
    {
        return 1;
    }

    srand((unsigned int)time(NULL));

    int temp = 25;       /* 섭씨 15~35 사이 변동 */
    int humidity = 50;   /* % 30~70 사이 변동 */
    int gas = 100;       /* ppm 0~950 사이 변동 */

    uint8_t packet[32];

    printf("UART 열림: %s, 1초마다 테스트 패킷 전송 시작 (Ctrl+C로 종료)\n", devPath);

    while (1)
    {
        time_t now = time(NULL);
        struct tm *lt = localtime(&now);

        temp = random_walk(temp, 1, 15, 35);
        humidity = random_walk(humidity, 2, 30, 70);
        gas = random_walk(gas, 20, 0, 950);

        uint8_t face, gasColor;
        if (gas >= GAS_DANGER_THRESHOLD)       { face = 2; gasColor = 2; }  /* 찡그림/위험 */
        else if (gas >= GAS_CAUTION_THRESHOLD) { face = 1; gasColor = 1; }  /* 무표정/주의 */
        else                                    { face = 0; gasColor = 0; }  /* 웃음/정상 */

        uint8_t data[11] = {
            face, gasColor,
            (uint8_t)((gas >> 8) & 0xFF), (uint8_t)(gas & 0xFF),
            (uint8_t)temp, (uint8_t)humidity,
            (uint8_t)lt->tm_hour, (uint8_t)lt->tm_min,
            (uint8_t)(lt->tm_year % 100), (uint8_t)(lt->tm_mon + 1), (uint8_t)lt->tm_mday
        };

        int len = build_packet(packet, data, sizeof(data), CMD_UPDATE);
        ssize_t written = write(fd, packet, len);
        if (written != len)
        {
            perror("write");
        }
        else
        {
            printf("전송: %02d:%02d %04d-%02d-%02d 온도%d 습도%d%% 가스%dppm 표정%d\n",
                   lt->tm_hour, lt->tm_min, lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                   temp, humidity, gas, face);
        }
        sleep(1);
    }

    close(fd);
    return 0;
}
