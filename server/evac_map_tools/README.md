# evac_map_tools - 평면도 -> HUB75 대피도/대피경로 변환 도구

평면도 이미지(PNG)를 STM32 HUB75 디스플레이용 **60x60 대피도 비트맵**과 **전광판별 대피 경로**로 변환하는 서버측 라이브러리 + 실행 예제.
서버(라즈베리파이 등)에서 이미지를 한 번 처리해 비트맵과 경로 좌표를 만들고, 그 결과를 STM32로 내려보내는 구조임.

> HUB75 패널 물리 해상도는 64x64지만, 테두리 2px(상하좌우 각 2줄, `HUB75_Shape13` 실측 확인)는 지도 데이터로 안 쓰고 빨간 점멸 경고 테두리용으로 비워둠 → 실제 맵은 60x60. 화면에 그릴 땐 STM32 쪽에서 좌표에 +2 오프셋을 줘서 패널 중앙에 그려야 함(`stm32_firmware/display_board`에 이미 반영됨).

## 0. 파일 구성

| 파일 | 역할 |
|---|---|
| `EvacPlanner.h` / `EvacPlanner.cpp` | 핵심 라이브러리. 이미지 → 비트맵 → BFS 경로 탐색 → (선택) STM32 `main.c` 자동 반영 |
| `server.cpp` | 사용 예제 겸 실행 진입점. `./evac_server [이미지] [main.c 경로]` |
| `Makefile` | 리눅스 빌드 |
| `map.png` | 입력 평면도 샘플 (벽 두껍게 보정한 최종본) |

## 1. 원본 이미지 준비 규칙

평면도 이미지에 아래 색으로 마커를 찍어야 함:

| 대상 | 색 | 비고 |
|---|---|---|
| 배경(통행 가능) | 흰색 | 원본 그대로 흰 바탕 |
| 전광판 위치 | 주황 사각형 | 여러 개 가능 (전광판 개수만큼) |
| 출구 | 연두색 바 | 여러 개 가능, **바깥 벽 경계선 위**에 걸치게 그려야 함 |
| 그 외 전부 | (아무 색이든) | 벽/기계/구조물 - 색 구분 없이 전부 "장애물"로 처리됨 |

> 판정 규칙: **흰색도 아니고 주황도 아니고 연두도 아니면 무조건 장애물.** 벽 색을 하나하나 등록할 필요 없음.

원본 이미지에서 마커 색이 살짝 다르면 `EvacPlanner.cpp` 상단의 `kWhiteLower/Upper`, `kDisplayMarker`, `kExitMarker` 값을 조정해야 함 (BGR 순서).

## 2. 빌드

OpenCV가 없으면 먼저 설치:
```
sudo apt install -y libopencv-dev build-essential pkg-config
```

빌드:
```
make
```
결과물: `evac_server`

정리:
```
make clean        # 오브젝트 파일 + 실행 파일 삭제
make clean_data   # 실행하며 생성된 txt/png 산출물 삭제
```

## 3. 실행

```
./evac_server [이미지경로] [main.c 경로]
```

- 이미지경로 생략 시 `map.png` 사용
- `main.c` 경로를 추가로 주면, **같은 이미지 분석 결과로 STM32 `main.c`의 `EVAC_DATA` 구역(`HUB75_EvacMap`/`EvacDisplays`/`EvacExits`)까지 자동으로 반영**됨 (`exportToMainC()`, 4번 API 참고). 마커(`USER CODE BEGIN/END EVAC_DATA`)를 못 찾으면 아무것도 안 건드리고 실패 로그만 찍음.
  ```
  ./evac_server map.png ../../stm32_firmware/display_board/Core/Src/main.c
  ```

실행하면 같은 폴더에 4개 파일이 생성됨:

| 파일 | 용도 |
|---|---|
| `evac_bitmap.txt` | 60x60 비트맵을 `0`/`1` 공백 구분 텍스트로 저장 (1=장애물) |
| `evac_debug.txt` | ASCII 격자 (`#`=장애물, `.`=빈공간, `S`=전광판, `E`=출구) - 좌표 단위 정밀 검증용 |
| `evac_preview.png` | 8배 확대 컬러 미리보기 (장애물=흰색, 전광판=주황점, 출구=초록점) - 눈으로 검증용 |
| `evac_routes.txt` | 전광판→출구 조합별 경로 waypoint 목록 (ID 포함) |

**STM32로 데이터 내리기 전에 `evac_preview.png`를 먼저 열어서 통로가 막히거나 벽이 사라진 곳이 없는지 확인할 것.**

## 4. API

```cpp
#include "EvacPlanner.h"

constexpr int GRID_SIZE = 60;          // 격자 한 변 크기

struct Point { int y, x; };            // 주의: (y, x) 순서

// 전체 파이프라인 실행 (비트맵/디버그/미리보기/경로 파일도 함께 저장)
std::vector<std::vector<Point>> processFloorPlan(const std::string& imagePath);

// 비트맵만 필요할 때 (파일 저장 없음)
std::vector<std::vector<int>> getEvacBitmap(const std::string& imagePath);

// 전광판 / 출구 좌표만 필요할 때 (Qt 등, 파일 저장 없음)
std::vector<Point> getEvacDisplays(const std::string& imagePath);
std::vector<Point> getEvacExits(const std::string& imagePath);

// 비트맵/전광판/출구를 STM32 main.c의 EVAC_DATA 구역에 C 배열로 자동 반영.
// main.c에서 마커를 못 찾으면 false(수동 반영 필요하다는 뜻).
bool exportToMainC(const std::string& imagePath, const std::string& mainCPath);
```

### `processFloorPlan()` 리턴값

- `routes[i][j]` = i번째 경로의 j번째 **꺾이는 지점** 좌표.
- 순서는 (전광판1→출구1, 출구2, …), (전광판2→출구1, …) 순. 즉 `전광판 수 × 출구 수` 개의 행.
- 도달 불가 경로는 **빈 행**으로 들어감.
- 어떤 행이 어느 전광판/출구인지 사람이 확인할 땐 `evac_routes.txt`를 보면 됨.

### `getEvacBitmap()` 리턴값

- `bitmap[y][x]` = 0(통행 가능) / 1(장애물), 60x60 고정.
- 이미지 로드 실패 시 빈 vector.
- 바깥 테두리는 출구 좌표를 제외하고 전부 벽(1)으로 막혀 있음.

### `exportToMainC()`

- 내부적으로 위 세 함수와 같은 이미지 분석(`analyzeFloorPlan()`)을 재사용 — 별도 도구 없이 이 라이브러리 하나로 STM32 컴파일타임 반영까지 끝남.
- 성공/실패만 반환(`bool`). 실패 사유(이미지 로드 실패, 마커 없음)는 stderr에 로그로 찍힘.

### `getEvacDisplays()` / `getEvacExits()` 리턴값

- 전광판/출구 좌표 목록. `Point{y, x}`.
- **순서가 `processFloorPlan()`의 경로 순서와 같음.** 전광판 수를 D, 출구 수를 E라 하면
  `routes[i * E + j]`가 `displays[i] → exits[j]` 경로임. Qt에서 특정 전광판·출구를 골라 그릴 때 이 규칙으로 인덱싱하면 됨.
- 이미지 로드 실패 시 빈 vector.

> 네 함수 모두 호출할 때마다 이미지를 다시 읽고 OpenCV 처리를 새로 함. 같은 이미지로 여러 개가 필요하면 한 번씩만 호출해 결과를 들고 있는 게 좋음.

## 5. 좌표계 / STM32 측 처리

- **좌표는 `{y, x}` 순서**임 (`Point.y`가 행, `Point.x`가 열). 배열 접근도 `bitmap[y][x]`. 값 범위 0~59.
- 이동은 4방향(상하좌우) 기준. 대각선 이동 가정 안 함 → 모든 경로 구간은 수평 아니면 수직임.
- **경로는 꺾이는 지점만 전송됨.** 직선 구간의 중간 좌표는 빼고 시작/끝만 보내므로, STM32가 연속한 두 waypoint 사이를 직접 이어 그려야 함. 브레젠험 알고리즘을 써도 되고, 어차피 수평/수직뿐이라 한 축만 증가시키는 단순 for문으로도 충분함. 패킷이 작아져 화재 시 UART 트래픽/지연이 줄어드는 게 목적.
- 전광판 좌표는 벽 셀이 아니라 **벽에 인접한 통행 가능한 칸**에 찍힘.
- 출구 좌표는 항상 `y==0`, `y==59`, `x==0`, `x==59` 중 하나 (바깥 경계선 위).
- **실제 64x64 물리 패널에 그릴 땐 좌표에 +2를 더해서 그려야 함** (예: `HUB75_SetPixel(x+2, y+2, ...)`) — 맨 바깥 2px 링이 빨간 점멸 경고 테두리 자리이기 때문.

## 6. 알고리즘 / 확장 계획 참고

- 경로 탐색은 전광판마다 BFS를 한 번 돌려 모든 출구까지의 최단 경로를 뽑는 방식 (그리드가 60x60 균일 비용이라 BFS가 Dijkstra/A*보다 단순하고 충분함).
- **화재/위험구역 표시는 구현됨** — 단, 이 라이브러리(`EvacPlanner`)가 아니라 UART 프로토콜(`drivers/stm_uart_display/stm_display_protocol.h`) 쪽에 있음. `CMD_EVAC_PATH`(0xB1, 경로 전용)와 별개로 `CMD_EVAC_FIRES`(0xB2)로 화재 좌표를 최대 6개까지 배열로 STM32에 보낼 수 있고, STM32가 노란색으로 반경만큼 표시함 (대피경로 자체는 빨강, 흐르는 이동 하이라이트는 자홍). `EvacPlanner`가 계산한 격자 좌표계(60x60, `{y,x}`)를 그대로 화재 좌표에도 써서 경로/화재/벽이 전부 같은 좌표계를 공유함. `EvacPlanner` 자체는 화재를 반영해 경로를 우회 계산하진 않음(화재 표시와 경로 계산은 독립적) — 서버(`server_main.cpp`)가 필요하면 직접 필터링해야 함.

## 7. 재생성이 필요한 경우

전광판 위치가 바뀌거나, 출구가 추가되거나, 평면도가 바뀌면:
1. 원본 이미지에 마커 다시 찍기
2. `./evac_server` 재실행
3. `evac_preview.png` / `evac_debug.txt`로 검증
4. 새 비트맵·경로 데이터를 STM32로 다시 전송
