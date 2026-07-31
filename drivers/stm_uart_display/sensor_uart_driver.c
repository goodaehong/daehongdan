/*
 * 실제 센서 값을 STM32 display_board로 UART 전송하는 라즈베리파이용 드라이버.
 *
 * 온습도(DHT22)/가스(MQ-9 via ADS1115)는 유나 팀원이 작성한 커널 드라이버
 * (drivers/dht22, drivers/gas_sensor)가 이미 GPIO/I2C를 점유하고 있으므로,
 * 이 프로그램은 하드웨어를 직접 건드리지 않고 그 드라이버들이 노출하는
 * sysfs 파일만 읽는다.
 *
 * 사전 준비 (라즈베리파이에서, 최초 1회):
 *   cd drivers/dht22 && make dt-apply && make load
 *   cd drivers/gas_sensor && make dt-apply && make load   (ads1015 블랙리스트 이슈 있으면 drivers/README.md 참고)
 *
 * sysfs 경로 확인 (커널/오버레이 버전에 따라 디렉토리명이 다를 수 있음):
 *   find /sys -name temp_value -o -name humid_value -o -name mq9_value
 * 위 결과와 아래 DHT22_TEMP_PATH 등 상수가 다르면 여기 경로만 맞춰 수정.
 *
 * 빌드: gcc sensor_uart_driver.c -o sensor_uart_driver
 * 실행: sudo ./sensor_uart_driver   (권한 문제 있으면 sudo)
 *
 * 주의: MQ-9는 아직 원시 ADC 값만 나오고(팀원이 환산식 미작성),
 *       GasRawToPpm()은 임시 선형 매핑임 - 실제 환산 로직 나오면 교체 필요.
 *
 * [수정] 센서 원시값이 초 단위로 미세하게 흔들리면서(예: 26.4 -> 26.6 -> 26.5)
 * 반올림 경계(x.5)를 계속 넘나들어 전광판 표시 숫자가 26/27/28 사이를
 * 빠르게 오가며 깜빡이는 문제가 있었음. 이동평균(EMA)으로 값을 스무딩해서
 * 해결함 - 실제 변화는 따라가되 노이즈는 눌러줌.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>

#define STX 0xAA
#define ETX 0x55
#define CMD_UPDATE 0x80

#define GAS_CAUTION_THRESHOLD 300
#define GAS_DANGER_THRESHOLD  700

/* 이동평균 계수 (0~1). 작을수록 더 부드럽게(느리게) 따라가고,
   클수록 센서 원시값에 더 빨리 반응함. 0.2~0.3 권장, 필요시 튜닝. */
#define SMOOTHING_ALPHA 0.25f

/* ===== 커널 드라이버가 노출하는 sysfs 경로 (환경에 따라 확인 후 수정) ===== */
#define DHT22_TEMP_PATH  "/sys/devices/platform/dht22/temp_value"
#define DHT22_HUMID_PATH "/sys/devices/platform/dht22/humid_value"
#define MQ9_RAW_PATH     "/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq9_value"

/* ===== UART 패킷 빌드 (send_test_uart.c와 동일한 프로토콜) ===== */
static int open_uart(const char *devPath)
{
    int fd = open(devPath, O_RDWR | O_NOCTTY);
    if (fd < 0)
    {
        perror("open uart");
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

/* ===== sysfs 값 읽기 헬퍼 ===== */
static int read_sysfs_float(const char *path, float *outValue)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        return -1;
    }
    int n = fscanf(f, "%f", outValue);
    fclose(f);
    return (n == 1) ? 0 : -1;
}

static int read_sysfs_long(const char *path, long *outValue)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        return -1;
    }
    int n = fscanf(f, "%ld", outValue);
    fclose(f);
    return (n == 1) ? 0 : -1;
}

/* 임시 선형 매핑: MQ-9 원시 ADC값 -> 0~999 표시값
   팀원이 실제 환산식(Rs/Ro 캘리브레이션 등) 작성하면 이 함수만 교체하면 됨 */
static uint16_t GasRawToPpm(long raw)
{
    if (raw < 0) raw = 0;
    long ppm = (raw * 999) / 32767;
    if (ppm > 999) ppm = 999;
    return (uint16_t)ppm;
}

/* 지수이동평균(EMA): prev와 new를 alpha 비율로 섞어서 부드럽게 갱신.
   센서 원시값이 반올림 경계를 미세하게 넘나들 때 전광판 숫자가
   튀는 현상을 줄여줌. */
static float ema_update(float prev, float newValue, float alpha)
{
    return prev * (1.0f - alpha) + newValue * alpha;
}

int main(void)
{
    const char *devPath = "/dev/serial0";
    int uartFd = open_uart(devPath);
    if (uartFd < 0)
    {
        return 1;
    }

    printf("센서 드라이버 시작 (DHT22 sysfs + MQ-9 via ADS1115 sysfs, 스무딩 alpha=%.2f)\n", SMOOTHING_ALPHA);

    /* lastTemp/lastHumidity: sysfs 읽기 실패 시 유지할 최근값
       smoothedTemp/smoothedHumidity/smoothedGas: EMA로 부드럽게 만든 값 (실제 전송에 사용) */
    float lastTemp = 25.0f, lastHumidity = 50.0f;
    float smoothedTemp = 25.0f, smoothedHumidity = 50.0f;
    float smoothedGas = 0.0f;
    int firstReading = 1;   /* 첫 값은 스무딩 없이 그대로 시작점으로 사용 */

    while (1)
    {
        float temp, humidity;
        if (read_sysfs_float(DHT22_TEMP_PATH, &temp) == 0)
        {
            lastTemp = temp;
        }
        else
        {
            fprintf(stderr, "DHT22 temp_value 읽기 실패 (%s), 이전 값 유지\n", DHT22_TEMP_PATH);
        }

        if (read_sysfs_float(DHT22_HUMID_PATH, &humidity) == 0)
        {
            lastHumidity = humidity;
        }
        else
        {
            fprintf(stderr, "DHT22 humid_value 읽기 실패 (%s), 이전 값 유지\n", DHT22_HUMID_PATH);
        }

        long mq9Raw = 0;
        uint16_t gasRawPpm = 0;
        if (read_sysfs_long(MQ9_RAW_PATH, &mq9Raw) == 0)
        {
            gasRawPpm = GasRawToPpm(mq9Raw);
        }
        else
        {
            fprintf(stderr, "MQ9 값 읽기 실패 (%s), 가스값 0으로 전송\n", MQ9_RAW_PATH);
        }

        /* 스무딩 적용: 첫 루프는 그대로 시작점으로 세팅, 이후부터는 EMA로 갱신 */
        if (firstReading)
        {
            smoothedTemp = lastTemp;
            smoothedHumidity = lastHumidity;
            smoothedGas = (float)gasRawPpm;
            firstReading = 0;
        }
        else
        {
            smoothedTemp = ema_update(smoothedTemp, lastTemp, SMOOTHING_ALPHA);
            smoothedHumidity = ema_update(smoothedHumidity, lastHumidity, SMOOTHING_ALPHA);
            smoothedGas = ema_update(smoothedGas, (float)gasRawPpm, SMOOTHING_ALPHA);
        }

        uint16_t gas = (uint16_t)(smoothedGas + 0.5f);

        time_t now = time(NULL);
        struct tm *lt = localtime(&now);

        uint8_t face, gasColor;
        if (gas >= GAS_DANGER_THRESHOLD)       { face = 2; gasColor = 2; }
        else if (gas >= GAS_CAUTION_THRESHOLD) { face = 1; gasColor = 1; }
        else                                    { face = 0; gasColor = 0; }

        uint8_t temp8 = (uint8_t)(smoothedTemp + 0.5f);
        uint8_t humidity8 = (uint8_t)(smoothedHumidity + 0.5f);

        uint8_t data[11] = {
            face, gasColor,
            (uint8_t)((gas >> 8) & 0xFF), (uint8_t)(gas & 0xFF),
            temp8, humidity8,
            (uint8_t)lt->tm_hour, (uint8_t)lt->tm_min,
            (uint8_t)(lt->tm_year % 100), (uint8_t)(lt->tm_mon + 1), (uint8_t)lt->tm_mday
        };

        uint8_t packet[32];
        int len = build_packet(packet, data, sizeof(data), CMD_UPDATE);
        ssize_t written = write(uartFd, packet, len);
        if (written != len)
        {
            perror("uart write");
        }
        else
        {
            printf("전송: %02d:%02d %04d-%02d-%02d 온도%.1f(raw%.1f) 습도%.1f%%(raw%.1f) 가스%dppm(raw=%ld) 표정%d\n",
                   lt->tm_hour, lt->tm_min, lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
                   smoothedTemp, lastTemp, smoothedHumidity, lastHumidity, gas, mq9Raw, face);
        }

        sleep(1);
    }

    close(uartFd);
    return 0;
}