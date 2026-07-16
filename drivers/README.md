# Drivers

리눅스 커널 디바이스 드라이버 모음. 센서/액추에이터 하드웨어를 유저스페이스에 노출.

## 구조

| 폴더 | 담당자 | 대상 하드웨어 | 상태 |
|---|---|---|---|
| `gas_sensor/` | 유나 | MQ-9(CO), MQ-2(연소성 가스) via ADS1115(I2C) | 개발 중 |
| `stm_uart_actuator/` | 유나 | STM32 액추에이터 보드 (밸브, 팬 제어) | 예정 |
| `stm_uart_display/` | 광렬 | STM32 LED 매트릭스 보드 | 예정 |

## 개발 환경

- Target: Raspberry Pi 4 (kernel 6.18.34+rpt-rpi-v8)
- Cross-compile: Ubuntu VM
- 커널 헤더 경로: `/lib/modules/6.18.34+rpt-rpi-v8/build`

## 빌드 방법

\```bash
cd drivers/gas_sensor
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
     -C /lib/modules/6.1.21-v8+/build M=$(pwd) modules
\```

## 설치 / 로드

\```bash
scp gas_sensor.ko yuna@<pi-ip>:~/
ssh yuna@<pi-ip>
sudo insmod gas_sensor.ko
dmesg | tail
\```

## Device Tree

각 드라이버는 대응하는 오버레이가 `dts/` 폴더에 있음. of_match_table 및 컴패터블 스트링은 커밋 메시지 또는 각 드라이버 소스 코드 상단 주석 참고.

## 참고

- 프로젝트 전체 개요는 최상위 [README.md](../README.md) 참고
- DT 오버레이 적용 방법은 [dts/README.md](../dts/README.md) 참고
