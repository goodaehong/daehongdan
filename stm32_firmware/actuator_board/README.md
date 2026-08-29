# actuator_board — 액추에이터 보드 펌웨어

## 📌 개요

- 서버 판단에 따라 **물리적 방어 조치**를 즉시 수행하는 STM32 펌웨어
- 비차단(Non-blocking) 하드웨어 타이머 + 상태 머신으로 다수 액추에이터 동시 제어
- 현재 상태를 서버에 되돌려 주는 양방향 동기화 지원

---

## ⚙️ 동작

### 제어 대상

| 대상 | 구동 방식 | 제어 |
| --- | --- | --- |
| 💧 펌프 · 솔레노이드 밸브 | 1CH 릴레이 + L298N 모터 드라이버 | 5V 워터 펌프 · 12V 밸브 개폐 |
| 🌀 환기팬 | 하드웨어 타이머 PWM | 4단계 — OFF / 약 / 중 / 강 |
| 🔊 사이렌 부저 | 패시브 부저 가변 주파수 PWM | 사이렌 멜로디 출력 |

### 노이즈 방어

- 대전류 부하 동작 시 전원 출렁임 · 역기전력으로 오작동 발생
- 하드웨어(LPF 필터)와 소프트웨어(핀 강제 고정)를 함께 적용해 차단
- 상세 경위 — 아래 🛠 문제 해결

### UART 프로토콜

가변 길이 패킷 프레이밍 + XOR 체크섬 검증 방식.

```
RX (요청)      [STX 0x02] [Length] [Command] [Data]      [Checksum] [ETX 0x03]
TX (응답/상태) [STX 0x02] [Length] [Command] [Data(상태)] [Checksum] [ETX 0x03]

체크섬 = [Length] ~ [Data] XOR 누적
```

| Command | 이름 | 하는 일 | 요청 데이터 |
| :---: | :--- | :--- | :--- |
| `0x10` | `CMD_FAN_CTRL` | 환기팬 구동 · 속도 제어 | `0x00` OFF · `0x01` 약 · `0x02` 중 · `0x03` 강 |
| `0x20` | `CMD_VALVE_CTRL` | 솔레노이드 가스 밸브 제어 | `0x00` 닫힘 · `0x01` 열림 |
| `0x30` | `CMD_SIREN_CTRL` | 사이렌 · 부저 제어 | `0x00` OFF · `0x01` ON |
| `0x40` | `CMD_REQ_STATUS` | 현재 상태 요청 | 없음 (3바이트 상태값 회신) |
| `0x50` | `CMD_GAS_EMERG` | [가스 누출] 사이렌 ON · 밸브 차단 · 환기팬 강 | 없음 |
| `0x60` | `CMD_MAX_EMERG` | [최고 비상] 사이렌 ON · 밸브 차단 · 환기팬 OFF | 없음 |
| `0x70` | `CMD_SYS_RESET` | [상황 해제] 사이렌 OFF · 밸브 개방 · 환기팬 약 | 없음 |

### 핀 배치

![STM32 핀아웃1](../images/STM32_1.png)
![STM32 핀아웃2](../images/STM32_2.png)
![STM32 핀아웃3](../images/STM32_3.png)

### 하드웨어·소프트웨어 인터페이스

![HSI](../images/HSI.png)

### 회로도

![회로도](../images/circuit_diagram.png)

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `Core/Src/main.c` | 진입점 · 명령 수신 · 처리 · 상태 머신 |
| `Core/Inc/main.h` | 핀 정의 · 핸들 |
| `FactorySafety.ioc` | CubeMX 설정 원본 — 핀 배치 · 클럭 · 타이머 |
| `Drivers/` | ST HAL · CMSIS (벤더 제공, 수정 안 함) |

---

## 🔧 빌드·실행

```bash
cmake -S stm32_firmware/actuator_board -B build && cmake --build build
```

플래싱은 ST-Link 로 합니다.

> ⚠️ CubeMX 로 코드를 재생성하면 `USER CODE BEGIN` ~ `END` 구간 밖은 사라집니다.

---

## 🛠 문제 해결

### 펌프 기동 시 부저가 제멋대로 울림

- **증상** — 펌프가 도는 순간 사이렌이 꺼진 상태에서도 소리가 남
- **원인** — 돌입 전류로 5V 메인 전원이 출렁이고, OFF 상태의 부저 핀이 안테나처럼 노이즈를 주움
- **조치 ① (하드웨어)** — 펌프 전원 입력단에 LPF(Low Pass Filter)를 설계해 전자기 노이즈 경로를
  물리적으로 격리
- **조치 ② (소프트웨어)** — 부저 OFF 시 핀을 `GPIO_PIN_RESET` 으로 0V 에 강제 고정
- **결과** — 오작동 완전 차단

---

## 🔗 참고

- 호스트 쪽 프로토콜 — [drivers/README.md](../../drivers/README.md)
- 서버 연동 — [server/actuator/README.md](../../server/actuator/README.md)
- 상위 개요 — [stm32_firmware/README.md](../README.md)
