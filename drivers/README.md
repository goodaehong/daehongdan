# Drivers

리눅스 커널 디바이스 드라이버 모음. 센서/액추에이터 하드웨어를 유저스페이스에 노출.

## 구조
 
| 폴더 | 담당자 | 대상 하드웨어 | 상태 |
|---|---|---|---|
| `gas_sensor/` | 김유나 | MQ-9(CO), MQ-2(연소성 가스) via ADS1115(I2C) | 완료 |
| `dht22/` | 김유나 | DHT22 온습도 센서 (GPIO 단일버스) | 완료 |
| `stm_uart_actuator/` | 김유나 | STM32 액추에이터 보드 (밸브, 팬, 사이렌 제어) — 유저스페이스 UART 프로토콜 라이브러리 | 완료 |
| `stm_uart_display/` | 김광렬 | STM32 LED 매트릭스 보드 — 유저스페이스 UART 프로토콜 라이브러리 | 완료 |

> `stm_uart_actuator/`, `stm_uart_display/`는 커널 모듈이 아니라 STM32와 통신하는 유저스페이스 프로토콜 라이브러리 + 테스트 프로그램입니다 (`gcc`/`g++`로 직접 빌드, `insmod` 아님). 빌드 방법은 [아래 섹션](#stm_uart_actuator--stm_uart_display-빌드-방법) 참고.

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

## stm_uart_actuator / stm_uart_display 빌드 방법

이 두 폴더는 커널 모듈이 아니라 일반 유저스페이스 프로그램입니다. Makefile 없이 `gcc`/`g++`로 직접 빌드합니다.

- `stm_display_protocol.c/h`: **서버 빌드에 실제로 포함되는 프로덕션 코드** (`server/CMakeLists.txt` 참고, 별도 빌드 불필요)
- 아래 프로그램들은 전부 **수동 테스트/하드웨어 점검용 독립 실행 파일**입니다. 최종 서버 빌드에는 포함되지 않고, 새 파이·새 보드를 처음 연결했을 때 통신 확인 용도로만 씁니다.

**액추에이터 테스트 프로그램**
```bash
cd drivers/stm_uart_actuator
gcc test_stm_actuator.c stm_actuator_protocol.c -o test_stm_actuator
sudo ./test_stm_actuator          # 기본 경로 /dev/ttyACM0
```
명령어: `fan off|low|mid|high`, `valve open|close`, `siren on|off`, `status`, `gas_emerg`, `max_emerg`, `reset`, `quit`

**전광판 테스트 프로그램**
```bash
cd drivers/stm_uart_display
gcc send_test_uart.c -o send_test_uart          # 임의 값으로 UART 송신 테스트
sudo ./send_test_uart
```
```bash
# 실제 센서값 + 위험/평상 전환까지 테스트 (server/sensors 코드 재사용)
g++ -std=c++17 test_alert_uart.cpp \
    ../../server/sensors/sensor_reader_hw.cpp \
    ../../server/sensors/sensor_conversion.cpp \
    stm_display_protocol.c \
    -I../../server/sensors -o test_alert_uart
sudo ./test_alert_uart          # 콘솔에 1=위험 전환, 2=평상 복귀
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

### STM32 보드 UART 장치 경로: `/dev/ttyACM0` 직접 지정 vs udev 심볼릭 링크

전광판 보드는 USART1(GPIO)에서 USART2(ST-Link USB VCP)로 전환되면서 `/dev/stm_display`라는 udev 심볼릭 링크로 여는 방식으로 바뀌었습니다 (`server_main.cpp`). 반면 액추에이터 보드는 `/dev/ttyACM0`을 직접 고정해서 씁니다.

두 STM32 보드가 모두 USB로 연결되는 환경이므로, 연결 순서나 재부팅에 따라 `ttyACM0`↔`ttyACM1`이 뒤바뀌면 액추에이터가 엉뚱한 포트를 열 수 있습니다. udev 규칙(idVendor/idProduct 또는 시리얼 기준)으로 두 보드 모두 고정 심볼릭 링크를 만드는 게 안전합니다.

**주의**: udev 규칙 파일 자체는 `/etc/udev/rules.d/`에 들어가는 시스템 설정이라 이 저장소에는 포함되어 있지 않습니다. `/dev/stm_display` 심볼릭 링크를 처음 만든 사람이 사용한 규칙 내용을 여기에 채워 넣어야 다른 팀원 파이에서도 재현 가능합니다. (`udevadm info -a -n /dev/ttyACM0`로 idVendor/idProduct 확인 가능)

### stm_actuator_protocol.c/h 서버 통합 진행 중

드라이버 쪽(`drivers/stm_uart_actuator/`)은 정리 완료: `StmActuator_*` 함수명을 `StmActuatorProtocol_*`로 전부 개명해서 전광판 쪽(`StmDisplayProtocol_*`)과 네이밍 규칙을 통일했고, 서버의 `Actuator_*`와도 안 헷갈리게 분리했습니다.

서버 쪽(`server/actuator/actuator_controller.cpp`)은 아직 체크섬·패킷 프레이밍·UART open을 자체 재구현한 옛날 코드 그대로라 `stm_actuator_protocol.c/h`를 안 씁니다. 전광판(`stm_display_protocol.c` + `server/CMakeLists.txt`)과 동일한 패턴으로 통합하는 작업 진행 중:

- `server/CMakeLists.txt`에 `STM_ACTUATOR_PROTOCOL_DIR` 추가, `USE_MOCK_ACTUATOR`가 아니면 `stm_actuator_protocol.c`를 빌드에 포함
- `actuator_control.h`가 `drivers/stm_uart_actuator/stm_actuator_protocol.h`를 include해서 STX/CMD_*/`StmActuatorStatus`를 그대로 재사용
- `actuator_controller.cpp`는 자체 UART 로직을 지우고 `StmActuatorProtocol_*` 호출로 교체

동작(체크섬 계산, 바이트 단위 읽기, termios 설정)은 기존과 100% 동일하고 구현 위치만 라이브러리로 옮기는 리팩터링입니다. 완료되면 이 항목 삭제.

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

### 4. `/dev/serial0`가 `ttyAMA0`인지 확인 (GPIO UART 사용하는 `sensor_uart_driver.c`/`send_test_uart.c`에만 해당)

> `test_alert_uart.cpp`는 USB VCP(`/dev/stm_display`)를 쓰므로 이 항목은 해당 없음.

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
