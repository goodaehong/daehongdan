# actuator — 밸브 · 환기팬 · 사이렌 제어

## 📌 개요

- STM32 액추에이터 보드에 UART 로 제어 명령 전송
- 자동 대응(판정 결과)과 수동 제어(Qt 버튼)를 같은 경로로 처리
- mock / 실물을 빌드 옵션으로 교체

---

## ⚙️ 동작

```
판정 결과 ──▶ ActuatorCommand ──▶ UART 패킷 ──▶ STM32 보드
Qt 버튼   ──────────┘
```

### 자동과 수동이 한 경로

- 경로를 나누면 장치 상태가 두 곳에서 갱신되어 어긋남
- 출처(`src`)만 구분해 기록에 남김 — `자동:gas` · `manual`
- 수동으로 조작한 장치는 자동 목표값과 달라도 정상으로 취급

### 판단 계층을 안 받음

| 받는 것 | 안 받는 것 |
| --- | --- |
| `ActuatorCommand{fan, valve, siren}` | 판정 결과 타입 |

- 판정 구조가 바뀌어도 이 모듈은 그대로
- 변환은 배선하는 쪽(`control_loop`)이 담당

### UART 패킷

| 구성 | 값 |
| --- | --- |
| 구조 | `STX(0x02)` · `Length` · `Command` · `Data` · `Checksum` · `ETX(0x03)` |
| 체크섬 | `Length` ~ `Data` XOR 누적 |
| 명령 | `0x10` 팬 · `0x20` 밸브 · `0x30` 사이렌 · `0x40` 상태 요청 |

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `actuator_control.h` | 명령·상태 구조 · 공개 함수 |
| `actuator_controller.cpp` | 실물 — UART 열기 · 패킷 조립 · 상태 폴링 |
| `actuator_control_mock.cpp` | mock — 상태만 보관, 전송 안 함 |

---

## 🔧 빌드·실행

```bash
cmake -S server -B server/build -DUSE_MOCK_ACTUATOR=ON   # mock
```

- 장치 경로는 `ACTUATOR_DEVICE` 로 주입 (기본 `/dev/stm_actuator`)
- 보드를 뽑았다 꽂으면 5초 주기로 재연결을 시도합니다

---

## 🛠 문제 해결

### 프로토콜 구현이 두 벌

- **상황** — `drivers/stm_uart_actuator/` 에 같은 프로토콜 구현이 있으나 서버는 자체 구현 사용
- **비교** — 두 구현의 패킷·체크섬은 동일함을 확인
- **판단** — 통합 가능하나, 검증이 끝난 경로를 건드리지 않기 위해 현행 유지
- 전광판은 반대로 드라이버 구현을 링크해서 씁니다 → [drivers/README.md](../../drivers/README.md)

### 보드 미인식

```bash
dmesg | tail          # USB disconnect 가 있으면 보드가 실제로 빠진 것
```

---

## 🔗 참고

- 펌웨어 · 핀맵 — [stm32_firmware/actuator_board/README.md](../../stm32_firmware/actuator_board/README.md)
- 상위 개요 — [server/README.md](../README.md)
