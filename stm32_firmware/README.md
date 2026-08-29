# stm32_firmware — 액추에이터 · 전광판 보드 펌웨어

## 📌 개요

- 서버(라즈베리파이)의 판단 결과를 받아 **물리 장치를 제어**하는 STM32 보드 2종
- 둘 다 UART 로 서버와 통신, 같은 패킷 구조 사용
- STM32CubeMX 로 생성한 프로젝트 구조

---

## ⚙️ 동작

```
서버 판단 ──▶ UART 패킷 ──▶ STM32
                             ├── 액추에이터 보드 — 밸브 · 환기팬 · 사이렌
                             └── 전광판 보드 — LED 매트릭스 화면
```

| 보드 | MCU | 역할 |
| --- | --- | --- |
| [`actuator_board/`](actuator_board/) | STM32F4 | 밸브 · 환기팬 PWM · 사이렌 부저 |
| [`display_board/`](display_board/) | STM32F4 | HUB75 LED 매트릭스 — 평상시 화면 · 대피 화면 |

### 공통 패킷 구조

```
[STX 0x02] [Length] [Command] [Data...] [Checksum] [ETX 0x03]
                    체크섬 = Length ~ Data 까지 XOR 누적
```

**명령 ID 는 보드마다 다릅니다.**

| 액추에이터 | | 전광판 | |
| --- | --- | --- | --- |
| `0x10` | 팬 제어 | `0x80` | 평상시 화면 갱신 |
| `0x20` | 밸브 제어 | `0x90` | 대피 화면 전환 |
| `0x30` | 사이렌 | `0xA0` | 평상 복귀 |
| `0x40` | 상태 요청 | `0xB0` | 상태 응답 (STM32 → Pi) |
| `0x50`~`0x70` | 비상 조합 · 해제 | `0xB1` `0xB2` | 대피경로 · 화재 위치 |

> ⚠️ 이 값들은 **펌웨어 · 호스트 드라이버 · 서버 세 곳에 각각 정의**되어 있습니다.
> 하나를 바꾸면 나머지도 같이 고쳐야 합니다. 컴파일은 통과하고 통신만 조용히 깨집니다.

---

## 📁 주요 파일

각 보드 폴더 구조는 CubeMX 표준을 따릅니다.

| 경로 | 내용 |
| --- | --- |
| `Core/Src/main.c` | 진입점 · UART 수신 · 명령 처리 |
| `Core/Inc/` | 헤더 |
| `Drivers/` | ST HAL · CMSIS (벤더 제공, 수정 안 함) |
| `*.ioc` | **CubeMX 설정 원본** — 핀 배치 · 클럭 · 주변장치 |
| `.mxproject` | CubeMX 생성 이력 — 코드 재생성 시 필요 |
| `cmake/` | CubeMX 가 생성한 빌드 설정 |

---

## 🔧 빌드·실행

**필요** — STM32CubeMX · STM32CubeIDE 또는 ARM 툴체인 (`arm-none-eabi-gcc`)

```bash
cmake -S stm32_firmware/actuator_board -B build && cmake --build build
```

플래싱은 ST-Link 로 합니다 (CubeIDE 또는 `st-flash`).

### 코드 재생성 시 주의

`.ioc` 를 열어 설정을 바꾸고 `GENERATE CODE` 를 누르면 `main.c` 가 다시 만들어집니다.

> ⚠️ **`USER CODE BEGIN` ~ `USER CODE END` 구간 밖에 쓴 코드는 사라집니다.**
> 전광판 `main.c` 의 `EVAC_DATA` 구역에는 대피 지도 배열이 들어 있습니다.
> `.mxproject` 가 있어야 CubeMX 가 보존 범위를 판단하므로 **지우면 안 됩니다.**

---

## 🔗 참고

- 호스트 쪽 프로토콜 라이브러리 — [drivers/README.md](../drivers/README.md)
- 서버 연동 — [server/actuator/](../server/actuator/) · [server/display/](../server/display/)
- 프로젝트 전체 개요 — [README.md](../README.md)
