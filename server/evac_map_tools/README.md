# evac_map_tools — 대피경로 계산 (EvacPlanner)

## 📌 개요

- 평면도 이미지에서 **벽 · 전광판 · 출구**를 자동 추출해 60×60 격자로 변환
- 다익스트라로 각 전광판 → 각 출구 경로 계산
- 화재 위치를 넣으면 그 구역을 피하는 경로로 재계산

---

## ⚙️ 동작

```
평면도 PNG ──▶ 마스킹·추출 ──▶ 60×60 격자 ──▶ 다익스트라 ──▶ 경로 좌표
                                    ▲
                              화재 좌표 (선택)
```

**색으로 요소를 구분합니다.** 벽 · 전광판 · 출구를 이미지 마스킹으로 자동 추출하므로,
평면도를 새로 그려도 규칙만 지키면 바로 씁니다.

**화재는 격자에 장애물로 찍습니다.** 화재 좌표 주변을 통행 불가로 만든 뒤 경로를 다시 찾습니다.
세로 방향은 `-2r ~ 0` 으로 **위로 더 넓게** 잡습니다 — 화재 좌표가 화염 박스의 바닥
접촉점이고, 연기·열기가 위로 오르기 때문입니다.

**도달 불가 경로도 자리를 지킵니다.** 결과는 `(전광판1→출구1,2,…)`, `(전광판2→…)` 순서의
평탄 배열이라, 빈 행을 빼면 인덱스가 밀려 서버가 엉뚱한 경로를 보냅니다.

**웨이포인트만 넘깁니다.** 꺾이는 지점만 STM32 로 보내고, 점과 점 사이는 수신 측이 직선으로
그립니다. 전광판 패킷 크기 제한(최대 30점) 때문입니다.

- 좌표계는 `{y, x}` 순서 — 전광판은 `{x, y}` 라 서버가 뒤집어 넘깁니다
- 격자 60×60 은 감지 좌표계와 통일한 값입니다 (팀 확정, 2026-08-19)

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `EvacPlanner.cpp/h` | 이미지 분석 · 격자 변환 · 경로 탐색 · STM32 코드 반영 |
| `evac_demo.cpp` | 단독 실행 데모 (`main()` — 서버 빌드에서 제외) |
| `map.png` | 기본 평면도 |
| `images/` | 전광판 표시 결과 사진 |

**실행 시 생성** (실행 파일과 같은 폴더, 커밋 안 함)

| 파일 | 내용 |
| --- | --- |
| `evac_bitmap.txt` | `0`(통행) · `1`(장애물) 60×60 격자 |
| `evac_preview.png` | 벽 · 전광판(파랑) · 출구(초록) 시각화 |
| `evac_routes.txt` | 전광판별 출구 웨이포인트 · 도달 가능 여부 |
| `evac_debug_start_N.txt` | 화재 위험구역(`*`) · 화재 중심(`F`) · 경로(`1`) 디버그 맵 |

---

## 🔧 빌드·실행

**필요** — OpenCV 4.5+

```bash
cd server/evac_map_tools
make                                    # 또는 cmake -S . -B build && cmake --build build
./evac_server map.png                   # 화재 없이 기본 경로
./evac_server map.png --fires 30,40,2   # 화재 우회 (x,y,반경)
```

**STM32 코드에 지도 반영**

```bash
./evac_server map.png ../../stm32_firmware/display_board/Core/Src/main.c
```

`main.c` 의 `USER CODE BEGIN EVAC_DATA` ~ `END` 구간에 격자·전광판·출구 배열을 자동으로
써 넣습니다. 마커를 못 찾으면 아무것도 건드리지 않고 실패를 알립니다.

**라이브러리로 쓰기**

```cpp
// 화재 없이
auto routes = processFloorPlan("map.png");

// 화재 우회
std::vector<FireCell> fires = {{30, 40, 2}};
auto routes = processFloorPlan("map.png", fires);
```

서버는 `server/floormap/` 을 통해 이 함수를 호출합니다.

---

## 🔗 참고

- 서버 연동 — [server/floormap/README.md](../floormap/README.md)
- 전광판 렌더링 — [stm32_firmware/display_board/README.md](../../stm32_firmware/display_board/README.md)
- 상위 개요 — [server/README.md](../README.md)
