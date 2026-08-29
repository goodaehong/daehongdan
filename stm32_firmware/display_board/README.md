# display_board — HUB75 LED 매트릭스 전광판

## 📌 개요

- 서버가 보낸 상태를 60×60 LED 매트릭스에 표시
- 평상 화면(가스 농도 · 온습도 · 표정 · 시각)과 대피 화면 두 가지
- 대피 화면에는 평면도 · 화재 위치 · 대피경로를 그림

---

## ⚙️ 동작

### 수신 명령

| 명령 | 내용 | 비고 |
| --- | --- | --- |
| `0x80` UPDATE | 평상 화면 갱신 | 1초 주기 |
| `0x90` ALERT | 대피 화면 전환 — 화재 / 가스 | 진입 시 1회 |
| `0xB2` EVAC_FIRES | 화재 위치 목록 | 최대 6곳 |
| `0xB1` EVAC_PATH | 대피경로 | 출구당 1패킷, 웨이포인트 최대 30 |
| `0xA0` CLEAR | 평상 복귀 | |
| `0xB0` ACK | 상태 응답 | STM32 → Pi |

### 대피 화면 렌더링

| 요소 | 표시 |
| --- | --- |
| 벽 | 흰색 |
| 전광판 | 노랑 |
| 출구 | 초록 3×3 — 한 픽셀은 잘 안 보여서 확대 |
| 대피경로 | 빨간 바탕 + **초록 하이라이트 3칸이 출발점→도착점으로 흐름** |
| 화재 | 바깥 빨강(불꽃) + 안쪽 노랑(중심), 매 프레임 크기 · 위치 흔들림 |

**경로는 2단계로 그림**

```
① 전체 경로를 빨강으로 모두 그리기
② 하이라이트를 그 위에 얹기
```

- 경로별로 번갈아 그리면 뒤 경로의 빨간칠이 앞 경로의 하이라이트를 덮음

**화재 좌표는 사각형의 「아랫변 중앙」**

- 카메라 쪽에서 화염 박스의 바닥 접촉점을 대표 위치로 잡아 전달
- 그래서 세로는 `-2r ~ 0` 으로 **위로만** 확장

**불을 흔드는 이유**

- 완전히 정적인 도형은 저해상도에서 「과녁」처럼 보임
- 50ms 마다 노란 코어의 크기와 위치를 ±1 흔들어 불처럼 읽히게 함

### 상수

| 상수 | 값 | 의미 |
| --- | --- | --- |
| 격자 | 60×60 | 서버 좌표계와 동일 (테두리 2px 제외한 실제 지도) |
| `EVAC_DISPLAY_COUNT` | 6 | 평면도에서 검출된 전광판 수 |
| `EVAC_EXIT_COUNT` | 4 | 출구 수 |
| `EVAC_PATH_MAX_WAYPOINTS` | 30 | 경로 하나의 최대 꺾임점 |
| `EVAC_MAX_FIRES` | 6 | 동시 표시 화재 최대 |
| `EVAC_PATH_CYCLE_MS` | 1300 | 하이라이트 한 바퀴 |
| `EVAC_ANIM_REDRAW_MS` | 50 | 애니메이션 갱신 (~20fps) |

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `Core/Src/main.c` | 명령 수신 · 처리 · 화면 상태 · 대피 지도 배열 |
| `Core/Src/hub75_display.c` | 패널 스캔 · 픽셀 버퍼 |
| `Core/Src/hub75_korean.c` | 한글 폰트 |
| `Core/Src/hub75_alphabet.c` · `hub75_number.c` · `hub75_tiny_number.c` | 영문 · 숫자 폰트 |
| `Core/Src/hub75_shape.c` | 도형 그리기 — 선 · 사각형 |
| `display-board.ioc` | CubeMX 설정 원본 |

---

## 🔧 빌드·실행

```bash
cmake -S stm32_firmware/display_board -B build && cmake --build build
```

플래싱은 ST-Link 로 합니다.

### 대피 지도 갱신

평면도가 바뀌면 `main.c` 의 `USER CODE BEGIN EVAC_DATA` ~ `END` 구간을 갱신해야 합니다.
직접 고치지 않고 도구가 자동 반영합니다.

```bash
cd server/evac_map_tools
./evac_server map.png ../../stm32_firmware/display_board/Core/Src/main.c
```

지도 격자 · 전광판 위치 · 출구 위치 배열이 함께 갱신됩니다.
마커를 못 찾으면 아무것도 건드리지 않고 실패를 알립니다.

> ⚠️ CubeMX 로 코드를 재생성하면 `USER CODE` 구간 밖은 사라집니다.
> 대피 지도 구역은 그 구간 안에 있어야 보존됩니다.

---

## 🔗 참고

- 경로 계산 · 지도 변환 — [server/evac_map_tools/README.md](../../server/evac_map_tools/README.md)
- 서버 연동 — [server/display/README.md](../../server/display/README.md)
- 상위 개요 — [stm32_firmware/README.md](../README.md)
