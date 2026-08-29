# drivers — 리눅스 커널 드라이버 · UART 프로토콜

## 📌 개요

- 센서 하드웨어를 sysfs 로 노출하는 **커널 모듈** 2종
- STM32 보드와 주고받는 **UART 프로토콜 라이브러리** 2종
- 하드웨어 점검용 **테스트 프로그램** 포함

---

## ⚙️ 동작

```
[센서]  ──▶ 커널 드라이버 ──▶ sysfs ──▶ 서버가 읽기만
[서버]  ──▶ 프로토콜 라이브러리 ──▶ UART ──▶ STM32 보드
```

| 폴더 | 종류 | 대상 하드웨어 |
| --- | --- | --- |
| `gas_sensor/` | 커널 모듈 | MQ-9(CO) · MQ-2(연소성 가스) via ADS1115 (I2C) |
| `dht22/` | 커널 모듈 | DHT22 온습도 (GPIO 단일버스) |
| `stm_uart_actuator/` | 프로토콜 + 테스트 | STM32 액추에이터 보드 (밸브 · 팬 · 사이렌) |
| `stm_uart_display/` | 프로토콜 + 테스트 | STM32 LED 매트릭스 보드 |

### 커널에서 처리하는 이유

- 센서 타이밍 제어를 커널이 담당 → 서버는 파일 읽기만 하면 됨
- DHT22 는 sysfs 를 읽는 순간 실제 측정 수행 (캐시 2초 — 스펙상 최소 측정 간격)

### 서버가 쓰는 것과 안 쓰는 것

| 구분 | 대상 |
| --- | --- |
| **서버 빌드에 포함** | `stm_display_protocol.c/h` — 전광판 패킷 |
| 테스트 전용 | 나머지 프로그램 — 새 파이 · 새 보드 연결 시 통신 확인용 |

- 프로토콜 라이브러리는 커널 모듈이 아님 — `gcc` / `g++` 로 직접 빌드

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `dht22/dht22_driver.c` | DHT22 커널 모듈 — 단일버스 타이밍 · 캐시 |
| `gas_sensor/ads1115_driver.c` | ADS1115 커널 모듈 — I2C · 채널별 원시값 |
| `stm_uart_actuator/stm_actuator_protocol.c/h` | 액추에이터 패킷 조립 · 체크섬 |
| `stm_uart_actuator/test_stm_actuator.c` | 액추에이터 수동 제어 테스트 |
| `stm_uart_display/stm_display_protocol.c/h` | 전광판 패킷 조립 — **서버가 링크** |
| `stm_uart_display/send_test_uart.c` | 임의 값 전송 테스트 |
| `stm_uart_display/sensor_uart_driver.c` | 실제 센서값 → 전광판 전송 테스트 |
| `stm_uart_display/test_alert_uart.cpp` | 위험 전환 · 대피경로 시나리오 테스트 |
| `stm_uart_display/fire_uart_test.cpp` | 화재 좌표를 경로 계산에 넣어 전송 (CMake 빌드) |

---

## 🔧 빌드·실행

**환경**

- Raspberry Pi 4 · kernel 6.18.34+rpt-rpi-v8
- 커널 헤더: `/lib/modules/6.18.34+rpt-rpi-v8/build`
- Pi 위에서 직접 빌드

### 커널 모듈

```bash
cd drivers/gas_sensor      # 또는 drivers/dht22
make
make dt-apply              # DT 오버레이 컴파일·적용
make load                  # sudo insmod
dmesg | tail
make unload                # sudo rmmod
```

- `dt-apply` 를 먼저 하지 않으면 드라이버가 프로브되지 않습니다
- 오버레이는 재부팅 시 사라지므로 매번 다시 적용해야 합니다 → [dts/README.md](../dts/README.md)

<details>
<summary><b>액추에이터 테스트</b></summary>

```bash
cd drivers/stm_uart_actuator
gcc test_stm_actuator.c stm_actuator_protocol.c -o test_stm_actuator
sudo ./test_stm_actuator          # 기본 경로 /dev/ttyACM0
```

명령: `fan off|low|mid|high` · `valve open|close` · `siren on|off` · `status` ·
`gas_emerg` · `max_emerg` · `reset` · `quit`

</details>

<details>
<summary><b>전광판 테스트</b></summary>

```bash
cd drivers/stm_uart_display
gcc send_test_uart.c -o send_test_uart
sudo ./send_test_uart
```

```bash
# 실제 센서값 + 위험/평상 전환 (server/sensors 재사용)
g++ -std=c++17 test_alert_uart.cpp \
    ../../server/sensors/sensor_reader_hw.cpp \
    ../../server/sensors/sensor_conversion.cpp \
    stm_display_protocol.c \
    -I../../server/sensors -o test_alert_uart
sudo ./test_alert_uart            # 1=위험 전환, 2=평상 복귀
```

```bash
# 화재 좌표 → 대피경로 실계산 → 전광판 전송 (OpenCV 필요)
cmake -S . -B build && cmake --build build
sudo ./build/fire_uart_test
```

</details>

<details>
<summary><b>실제 센서 테스트 전 체크리스트</b> — 하나라도 빠지면 원인 찾기 어려운 통신 실패로 이어집니다</summary>

**① 커널 드라이버 로드**

```bash
lsmod | grep -E "dht22|ads1115"
dmesg | tail -20      # dht22 driver probed / ADS1115 probed at addr 0x48
```

**② ADS1015 블랙리스트** — 아래 문제 해결 1번. 새 파이라면 거의 확실히 재현됩니다.

**③ sysfs 경로 확인**

```
/sys/devices/platform/dht22/temp_value
/sys/devices/platform/dht22/humid_value
/sys/devices/platform/soc/fe804000.i2c/i2c-1/1-0048/mq9_value
```

MQ9 경로는 실제 파이에서 검증된 값입니다. DHT22 경로는 device tree 가 플랫폼 디바이스를
어떤 이름으로 등록하느냐에 따라 달라질 수 있어 미검증 상태입니다. 다르면
`sensor_uart_driver.c` 상단의 경로 상수만 맞춰 수정하면 됩니다.

```bash
find /sys -name temp_value -o -name humid_value -o -name mq9_value 2>/dev/null
```

**④ `/dev/serial0` 가 `ttyAMA0` 인지** — 아래 문제 해결 2번. GPIO UART 를 쓰는
`sensor_uart_driver.c` · `send_test_uart.c` 에만 해당합니다.

</details>

---

## 🛠 문제 해결

### 1. 배포판 내장 드라이버가 ADS1115 를 먼저 잡음

- **증상** — 모듈을 올려도 `mq2_value` / `mq9_value` sysfs 노드가 안 생김
- **원인** — 커널에 내장된 `ti_ads1015` 가 ADS1013/1014/1015/**1115** 를 모두 지원.
  DT 오버레이의 `compatible = "ti,ads1115"` 를 보고 커스텀 드라이버보다 **먼저 바인딩**되어
  프로브 자체가 호출되지 않음
- **확인**

```bash
ls -l /sys/bus/i2c/devices/1-0048/driver
# .../drivers/ads1015     → 원인 확정
# .../drivers/ads1115_gas → 정상
```

- **조치** — 내장 드라이버를 블랙리스트에 등록

```bash
sudo rmmod ti_ads1015
echo "blacklist ti_ads1015" | sudo tee /etc/modprobe.d/blacklist-ads1015.conf
sudo insmod ads1115_driver.ko
```

> ⚠️ 블랙리스트 설정은 `/etc/modprobe.d/` 에 들어가는 시스템 설정이라 저장소에 없습니다.
> **새 파이마다 최초 1회** 실행해야 합니다.

### 2. GPIO UART 통신이 조용히 깨짐

- **증상** — 패킷을 보내도 반응이 없거나, STM32 가 응답을 보내는 순간부터 통신이 깨짐
- **원인 ①** — 기본 UART(GPIO14/15)가 Bluetooth 에 밀려 정확도 낮은 mini-UART(`ttyS0`)로 잡힘
- **원인 ②** — 시리얼 콘솔 서비스가 같은 포트를 점유
- **확인·조치**

```bash
ls -l /dev/serial0        # ttyAMA0 정상 / ttyS0 이면 아래 조치
```

```bash
sudo sh -c 'echo "dtoverlay=disable-bt" >> /boot/firmware/config.txt'
sudo systemctl disable hciuart
sudo reboot
```

```bash
systemctl status serial-getty@ttyAMA0.service
sudo systemctl stop serial-getty@ttyAMA0.service
sudo systemctl disable serial-getty@ttyAMA0.service
```

- USB VCP 를 쓰는 `test_alert_uart.cpp` 는 이 문제와 무관합니다

### 3. STM32 장치 경로가 뒤바뀜

| 보드 | 현재 방식 | 상태 |
| --- | --- | --- |
| 전광판 | `/dev/stm_display` — udev 심볼릭 링크 | 해결 |
| 액추에이터 | `/dev/ttyACM0` — 직접 고정 | 미적용 |

- **원인** — 두 보드 모두 USB 연결이라 연결 순서 · 재부팅에 따라 `ttyACM0` ↔ `ttyACM1` 이 바뀜
- **조치** — 전광판처럼 udev 규칙(idVendor/idProduct 또는 시리얼 기준)으로 고정하면 안전

```bash
udevadm info -a -n /dev/ttyACM0     # idVendor / idProduct 확인
```

> ⚠️ udev 규칙 파일은 `/etc/udev/rules.d/` 에 들어가는 시스템 설정이라 저장소에 없습니다.

### 4. 액추에이터 프로토콜 구현이 두 벌

| 보드 | 구현 |
| --- | --- |
| 전광판 | 이 폴더의 라이브러리를 서버가 링크 — **1벌** |
| 액추에이터 | 서버가 체크섬 · 패킷 · UART 를 자체 재구현 — **2벌** |

- **확인** — 두 구현의 패킷 · 체크섬 동작은 동일
- **영향** — 프로토콜 변경 시 양쪽을 같이 고쳐야 함
- **통합 방법** — `server/CMakeLists.txt` 에 소스를 추가하고, 서버의 자체 로직을
  라이브러리 호출로 교체
- **현재 판단** — 검증이 끝난 경로를 건드리지 않기 위해 현행 유지

---

## 🔗 참고

- DT 오버레이 적용 — [dts/README.md](../dts/README.md)
- 프로젝트 전체 개요 — [README.md](../README.md)
