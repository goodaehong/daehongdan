# image_to_bitmap - 평면도 -> HUB75 대피도 변환 도구

평면도 이미지(PNG)를 STM32 HUB75 디스플레이용 62x62 대피도 데이터(C 배열)로 변환하는 빌드타임 도구.
STM32나 라즈베리파이에서 돌리는 게 아니라, **이 데이터를 미리 한 번 뽑아서 `main.c`에 박아넣기 위한 오프라인 변환기**임.

> HUB75 패널 물리 해상도는 64x64지만, 테두리 1px(상하좌우 각 1줄)은 지도 데이터로 안 쓰고 빨간 점멸 경고 테두리용으로 비워둠 → 실제 맵은 62x62. 화면에 그릴 땐 STM32 쪽에서 좌표에 +1 오프셋을 줘서 패널 중앙에 그려야 함.

## 1. 원본 이미지 준비 규칙

평면도 이미지에 아래 색으로 마커를 찍어야 함:

| 대상 | 색 | 비고 |
|---|---|---|
| 배경(통행 가능) | 흰색 | 원본 그대로 흰 바탕 |
| 전광판 위치 | 주황 사각형 | 여러 개 가능 (전광판 개수만큼) |
| 출구 | 연두색 바 | 여러 개 가능, **바깥 벽 경계선 위**에 걸치게 그려야 함 |
| 그 외 전부 | (아무 색이든) | 벽/기계/구조물 - 색 구분 없이 전부 "장애물"로 처리됨 |

> 판정 규칙: **흰색도 아니고 주황도 아니고 연두도 아니면 무조건 장애물.** 벽 색을 하나하나 등록할 필요 없음.

원본 이미지에서 마커 색이 살짝 다르면 `image_to_bitmap.cpp` 상단의 `kWhiteLower/Upper`, `kDisplayMarker`, `kExitMarker` 값을 조정해야 함 (`probe_color.exe`로 실제 픽셀 색 뽑아서 확인 가능).

## 2. 빌드

**Windows (로컬 개발용, OpenCV는 vcpkg로 설치돼있어야 함):**
```
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```
결과물: `build/Release/image_to_bitmap.exe`, `build/Release/probe_color.exe`

**리눅스(서버/라즈베리파이 등) - cmake 안 씀, g++로 바로 컴파일:**

OpenCV가 없으면 먼저 설치:
```
sudo apt install -y libopencv-dev build-essential pkg-config
```

그 다음 컴파일 (cmake/vcpkg 관련 명령어는 Windows 전용이니 리눅스에서는 절대 쓰지 말 것):
```
g++ -std=c++17 image_to_bitmap.cpp -o image_to_bitmap `pkg-config --cflags --libs opencv4`
g++ -std=c++17 probe_color.cpp -o probe_color `pkg-config --cflags --libs opencv4`
```

## 3. 실행

```
image_to_bitmap.exe <평면도 이미지 경로>
```

같은 폴더에 3개 파일이 생성됨:

| 파일 | 용도 |
|---|---|
| `evac_bitmap.c` | **최종 산출물.** `main.c`에 그대로 붙여넣을 C 배열 |
| `evac_preview.png` | 8배 확대 컬러 미리보기 (장애물=흰색, 전광판=주황점, 출구=초록점) - 결과 눈으로 검증용 |
| `evac_debug.txt` | ASCII 격자 텍스트 (`#`=장애물, `.`=빈공간, `5`=전광판, `E`=출구) - 좌표 단위로 정밀 검증할 때 |

**main.c에 붙여넣기 전에 `evac_preview.png`를 먼저 열어서 통로가 막히거나 벽이 사라진 곳이 없는지 확인할 것.**

## 4. 산출물(`evac_bitmap.c`) 데이터 구조

```c
// 0=빈공간(통행가능), 1=장애물
const uint8_t HUB75_EvacMap[62][62] = { ... };

const uint8_t EvacDisplays[][2] = {   // 전광판 위치들
  {27,60}, {60,38}, {11,32}, ...
};
#define EVAC_DISPLAY_COUNT 6

const uint8_t EvacExits[][2] = {      // 출구 위치들
  {48,61}, {0,48}, {61,16}, {22,0}
};
#define EVAC_EXIT_COUNT 4
```

- **좌표계**: `[x][y]` 아님, `[y][x]`(행 우선) 배열이지만 좌표 자체는 `{x, y}` 순서. `HUB75_EvacMap[y][x]`로 접근. 값 범위는 0~61.
- **`HUB75_EvacMap`**: 0=통행 가능, 1=장애물(벽/기계/구조물 전부 포함, 종류 구분 없음). 바깥 테두리는 출구 좌표를 제외하고 전부 벽으로 막혀있음.
- **`EvacDisplays[i][0]`=x, `[i][1]`=y**: 전광판이 실제로 서있는 칸이 아니라, **벽에서 정확히 1칸 떨어진 통행 가능한 칸**. (벽 셀 자체에는 절대 안 찍힘)
- **`EvacExits[i][0]`=x, `[i][1]`=y**: 항상 `x==0`, `x==61`, `y==0`, `y==61` 중 하나 (62x62 맵 기준 바깥 경계선 위).
- 배열 크기는 고정 길이가 아니라 이미지에 찍힌 마커 개수만큼 나오므로, 반드시 `EVAC_DISPLAY_COUNT`/`EVAC_EXIT_COUNT`로 순회할 것 (하드코딩 인덱스 금지).
- **실제 64x64 물리 패널에 그릴 땐 이 좌표에 그대로 +1을 더해서 그려야 함** (예: `HUB75_SetPixel(x+1, y+1, ...)`) — 맨 바깥 1px 링이 빨간 점멸 경고 테두리 자리이기 때문.

## 5. 알고리즘/서버 작업 시 참고

- 이동은 4방향(상하좌우) 기준으로 설계됨 (대각선 이동 가정 안 함).
- 목표: 특정 전광판 위치(`EvacDisplays[i]`)에서 `EvacExits[]` 중 가장 가까운 출구까지 BFS로 최단 경로 탐색 예정 (그리드가 62x62 균일 비용이라 BFS가 Dijkstra/A*보다 단순하고 충분함).
- 화재/위험구역 표시는 아직 미구현. 기존 알림 패킷(CMD 0x90)의 `zoneID` 필드(카메라 채널 `detCh` 매핑)를 재사용해서, 특정 구역을 동적으로 장애물 취급하는 방식으로 확장할 계획.

## 6. 재생성이 필요한 경우

전광판 위치가 바뀌거나, 출구가 추가되거나, 평면도가 바뀌면:
1. 원본 이미지에 마커 다시 찍기
2. `image_to_bitmap.exe <이미지>` 재실행
3. `evac_preview.png`로 검증
4. `evac_bitmap.c` 내용을 `main.c`에 다시 붙여넣기
