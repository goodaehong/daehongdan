# display — 전광판 (STM32 LED 매트릭스)

## 📌 개요

- STM32 전광판 보드에 UART 로 화면 갱신·대피 화면 전환 명령 전송
- 평상시 화면(가스 농도·온습도·표정)과 대피 화면(화재/가스) 두 가지
- mock / 실물을 빌드 옵션으로 교체

---

## ⚙️ 동작

```
평상시  DisplayUpdate ──▶ CMD_UPDATE(0x80)  1초 주기
위험    DisplayDisaster ──▶ CMD_ALERT(0x90) 진입 시 1회
        + 화재 위치 CMD_EVAC_FIRES(0xB2) · 경로 CMD_EVAC_PATH(0xB1)
해제    CMD_CLEAR(0xA0)
```

**판단 계층의 타입을 직접 받지 않습니다.** `DisplayUpdate` · `DisplayDisaster` 로만 받아,
판정 구조가 바뀌어도 이 모듈은 그대로 둡니다.

**표현 변환은 이 모듈 안에서 합니다.** 가스 4자리 클램프, `float` → `uint8` 반올림,
시각 조회가 여기 있어서 화면 표현을 바꿔도 호출부를 안 건드립니다.
단 **표시 단계(`gasLevel`)와 재난 종류 판별은 판단 기준**이라 밖에서 정해 넘깁니다.

**표정·그래프는 가스 농도로만 결정됩니다.** 화재·연기만 감지되고 가스가 정상인데도
표정이 빨갛게 뜨던 문제 때문입니다. 화재·연기 경보는 대피 화면(`SendAlert`)이 따로 처리합니다.

**대피경로는 좌표가 바뀔 때만 보냅니다.** 매초 보내면 출구 수만큼 패킷이 반복되고
화면도 깜빡입니다.

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `stm_display.h` | `DisplayUpdate` · `DisplayDisaster` · 공개 함수 |
| `stm_display.cpp` | 실물 — 패킷 전송 · ACK 확인 · 재연결 |
| `stm_display_mock.cpp` | mock — 성공만 리턴 |

---

## 🔧 빌드·실행

```bash
cmake -S server -B server/build -DUSE_MOCK_DISPLAY=ON   # mock
```

- 장치 경로는 `STM_DISPLAY_DEVICE` 로 주입 (기본 `/dev/stm_display`)
- 패킷 조립은 `drivers/stm_uart_display/stm_display_protocol.c` 를 링크해서 씁니다

---

## 🛠 문제 해결

- **ACK 없음** — `StmDisplay_GetLinkOk()` 가 `false`. 보드 전원·UART 연결 확인
- **패킷 깨짐** — 연달아 쏘면 STM32 수신 버퍼가 넘칩니다. 패킷 사이 20ms 대기를 둡니다
- **전송 최대치** — 웨이포인트 30개 · 화재 6곳. 넘으면 전송하지 않고 `false` 리턴

---

## 🔗 참고

- 펌웨어 · 렌더링 — [stm32_firmware/display_board/README.md](../../stm32_firmware/display_board/README.md)
- 상위 개요 — [server/README.md](../README.md)
