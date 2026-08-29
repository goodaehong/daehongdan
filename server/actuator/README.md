# actuator — 밸브 · 환기팬 · 사이렌 제어

## 📌 개요

- STM32 액추에이터 보드에 UART 로 제어 명령 전송
- 자동 대응(판정 결과)과 수동 제어(Qt 버튼)를 같은 경로로 처리
- mock / 실물을 빌드 옵션으로 교체

---

## ⚙️ 동작

```
판정 결과 ──▶ ActuatorCommand ──▶ UART 패킷 ──▶ STM32
Qt 버튼   ──▶ Actuator_Execute ──┘
```

**자동과 수동이 같은 경로로 들어갑니다.** 경로를 나누면 장치 상태가 두 곳에서 갱신되어
어긋납니다. 대신 `src` 값으로 출처를 구분해 기록에 남깁니다 (`자동:gas` · `manual`).

**판단 계층의 타입을 직접 받지 않습니다.** `ActuatorCommand{fan, valve, siren}` 으로만 받아,
판정 구조가 바뀌어도 이 모듈은 그대로 둡니다. 변환은 배선하는 쪽(`control_loop`)이 합니다.

**상태를 추적합니다.** 수동으로 조작한 장치는 자동 목표값과 달라도 정상으로 봅니다
(`ActuatorSnapshot` 의 `fanSrc` · `valveSrc` · `sirenSrc`).

| 패킷 | 내용 |
| --- | --- |
| 구조 | `STX(0x02)` · `Length` · `Command` · `Data` · `Checksum` · `ETX(0x03)` |
| 체크섬 | `Length` ~ `Data` XOR 누적 |
| 명령 | `0x10` 팬 · `0x20` 밸브 · `0x30` 사이렌 · `0x40` 상태 요청 |

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `actuator_control.h` | `ActuatorCommand` · `ActuatorSnapshot` · CMD 상수 |
| `actuator_controller.cpp` | 실물 — UART 열기 · 패킷 조립 · 상태 폴링 |
| `actuator_control_mock.cpp` | mock — 상태만 들고 있고 전송 안 함 |

---

## 🔧 빌드·실행

```bash
cmake -S server -B server/build -DUSE_MOCK_ACTUATOR=ON   # mock
```

- 장치 경로는 `ACTUATOR_DEVICE` 로 주입 (기본 `/dev/stm_actuator`)
- 보드를 뽑았다 꽂으면 `Actuator_Poll()` 이 5초 주기로 재연결을 시도합니다

---

## 🛠 문제 해결

- **보드 미인식** — `dmesg` 에 `USB disconnect` 가 있으면 보드가 실제로 빠진 것입니다
- **프로토콜 중복** — `drivers/stm_uart_actuator/stm_actuator_protocol.c` 에 같은 구현이
  있으나 서버는 자체 구현을 씁니다 → [drivers/README.md](../../drivers/README.md) 문제 해결 3번

---

## 🔗 참고

- 펌웨어 · 핀맵 — [stm32_firmware/actuator_board/README.md](../../stm32_firmware/actuator_board/README.md)
- 상위 개요 — [server/README.md](../README.md)
