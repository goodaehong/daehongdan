# Drivers

리눅스 커널 디바이스 드라이버 모음. 센서/액추에이터 하드웨어를 유저스페이스에 노출.

## 구조
 
| 폴더 | 담당자 | 대상 하드웨어 | 상태 |
|---|---|---|---|
| `gas_sensor/` | 김유나 | MQ-9(CO), MQ-2(연소성 가스) via ADS1115(I2C) | 완료 |
| `dht22/` | 김유나 | DHT22 온습도 센서 (GPIO 단일버스) | 완료 |
| `stm_uart_actuator/` | 김유나 | STM32 액추에이터 보드 (밸브, 팬, 사이렌 제어) — 유저스페이스 UART 프로토콜 라이브러리 | 완료 |
| `stm_uart_display/` | 김광렬 | STM32 LED 매트릭스 보드 — 유저스페이스 UART 프로토콜 라이브러리 | 완료 |

## 개발 환경

- Target: Raspberry Pi 4 (kernel 6.18.34+rpt-rpi-v8)
- Build: Pi 위에서 직접 빌드
- 커널 헤더 경로: `/lib/modules/6.18.34+rpt-rpi-v8/build`

## 빌드 방법

Pi에 SSH(VSCode Remote-SSH 등)로 접속한 상태에서, 각 드라이버 폴더 안의 Makefile로 직접 빌드합니다.
 
```bash
cd drivers/gas_sensor      # 또는 drivers/dht22
make
```
 
내부적으로는 다음과 동일합니다:
 
```bash
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
```

## 설치 / 로드

각 드라이버 폴더의 Makefile에 `load`/`unload` 타겟이 정의되어 있습니다.
 
```bash
cd drivers/gas_sensor      # 또는 drivers/dht22
make load                   # sudo insmod
dmesg | tail
make unload                 # sudo rmmod
```

## Device Tree
각 드라이버는 대응하는 오버레이가 `dts/` 폴더에 있음. of_match_table 및 컴패터블 스트링은 커밋 메시지 또는 각 드라이버 소스 코드 상단 주석 참고.
 
GPIO 기반 드라이버(`dht22`)는 Makefile에 `dt-apply`/`dt-remove` 타겟이 추가로 있어 오버레이 컴파일·적용까지 한 번에 처리합니다.
 
```bash
cd drivers/dht22
make dt-apply
make load
```
 
I2C 기반 드라이버(`gas_sensor`)도 동일한 패턴을 따릅니다.

## 알려진 이슈
 
### gas_sensor: mainline `ads1015` 드라이버 충돌
 
라즈베리파이 배포판 커널에 ADS1015 계열 mainline 드라이버(`ti_ads1015`)가 기본 내장되어 있습니다. 이 드라이버는 ADS1013/1014/1015/**1115**를 전부 지원하도록 만들어져 있어서, DT overlay의 `compatible = "ti,ads1115"`를 보는 순간 우리 커스텀 드라이버(`ads1115_gas`)보다 먼저 자동으로 바인딩됩니다. 그 결과 커스텀 드라이버는 디바이스를 점유하지 못해 `probe()`가 호출되지 않습니다.
 
**증상**: `insmod ads1115_driver.ko` 해도 `mq2_value`/`mq9_value` sysfs 노드가 생성되지 않음
 
**확인**:
```bash
ls -l /sys/bus/i2c/devices/1-0048/driver
# -> .../drivers/ads1015 로 나오면 원인 확정 (정상은 .../drivers/ads1115_gas)
```
 
**해결 (Pi마다 최초 1회 필요)**:
 
블랙리스트 설정은 `/etc/modprobe.d/`에 들어가는 시스템 설정이라 git 저장소에는 포함되지 않습니다. **새 라즈베리파이에 이 드라이버를 설치하는 팀원은 각자 아래 명령을 한 번씩 실행**해야 합니다.
 
```bash
sudo rmmod ti_ads1015
echo "blacklist ti_ads1015" | sudo tee /etc/modprobe.d/blacklist-ads1015.conf
sudo insmod ads1115_driver.ko
```
 
이후 확인:
```bash
ls -l /sys/bus/i2c/devices/1-0048/driver
# -> .../drivers/ads1115_gas 로 나오면 정상
```

## stm_uart_display 실제 센서 테스트 전 체크리스트

`drivers/stm_uart_display/sensor_uart_driver.c`(실제 센서값을 STM32로 전송하는 프로그램)를
본인 파이가 아닌 다른 팀원의 라즈베리파이(센서가 실제로 연결된 환경)에서 테스트할 때,
아래 4가지를 순서대로 확인해야 합니다. 하나라도 빠지면 원인 찾기 어려운 통신 실패로 이어짐.

### 1. 커널 드라이버 로드 확인

```bash
lsmod | grep -E "dht22|ads1115"
```

- 아무것도 안 뜨면 아직 로드 안 된 것 → [빌드 방법](#빌드-방법), [설치 / 로드](#설치--로드) 섹션대로 `dht22`, `gas_sensor` 순서로 `make` → `make dt-apply` → `make load` 진행
- 로드 후 `dmesg | tail -20`으로 `dht22 driver probed`, `ADS1115 probed at addr 0x48` 같은 로그가 찍혔는지 확인. `probe failed` 등 에러가 있으면 그 로그부터 확인.

### 2. ADS1015 블랙리스트 이슈

[알려진 이슈 - gas_sensor: mainline `ads1015` 드라이버 충돌](#알려진-이슈) 참고.
파이마다 최초 1회 블랙리스트 설정이 필요하므로, 새로 테스트하는 파이라면 거의 확실히 이 문제가 재현됨.

```bash
ls -l /sys/bus/i2c/devices/1-0048/driver
# -> .../drivers/ads1015 로 나오면 아직 안 된 것, .../drivers/ads1115_gas 면 정상
```

### 3. sysfs 경로 확인

`sensor_uart_driver.c`가 읽는 경로:
```
/sys/devices/platform/dht22/temp_value
/sys/devices/platform/dht22/humid_value
/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq9_value
```

MQ9 경로는 유나 팀원이 실제 파이에서 검증해서 확정한 값(`/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/`)입니다. DHT22 쪽 경로(`/sys/devices/platform/dht22/`)는 device tree가 실제로 어떤 이름으로 플랫폼 디바이스를 등록하느냐에 따라 달라질 수 있어 아직 미검증 상태입니다. 실제 파이에서 아래로 검증 후, 다르면 `sensor_uart_driver.c` 상단의 경로 상수만 맞춰서 수정.

```bash
find /sys -name temp_value -o -name humid_value -o -name mq9_value 2>/dev/null
```

### 4. `/dev/serial0`가 `ttyAMA0`인지 확인

라즈베리파이 기본 UART(`GPIO14/15`)가 Bluetooth에 밀려 정확도 낮은 mini-UART(`ttyS0`)로 잡히는 경우가 있음. 새로 설정하는 파이라면 재현 가능성 높음.

```bash
ls -l /dev/serial0
# -> ttyAMA0 면 정상, ttyS0 면 아래 조치 필요
```

`ttyS0`인 경우:
```bash
sudo sh -c 'echo "dtoverlay=disable-bt" >> /boot/config.txt'   # 구버전 OS는 /boot/firmware/config.txt
sudo systemctl disable hciuart
sudo reboot
```

추가로 시리얼 콘솔(로그인 셸)이 같은 포트를 붙잡고 있으면 STM32가 ACK 응답을 보내는 순간부터 통신이 깨질 수 있음:
```bash
systemctl status serial-getty@ttyAMA0.service
# active (running)이면:
sudo systemctl stop serial-getty@ttyAMA0.service
sudo systemctl disable serial-getty@ttyAMA0.service
```

## 참고

- 프로젝트 전체 개요는 최상위 [README.md](../README.md) 참고
- DT 오버레이 적용 방법은 [dts/README.md](../dts/README.md) 참고
