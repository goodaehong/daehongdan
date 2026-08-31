# debug — 감지 · 좌표 디버그 실행기

## 📌 개요

- Qt 와 서버 없이 **감지 결과만 눈으로 확인**하는 Windows 실행기
- 화재 · 연기 검출을 실행하고 화재 박스의 60×60 좌표 변환을 확인
- 마우스로 감지 제외 영역을 그려 즉시 확인 가능

---

## ⚙️ 동작

```
RTSP / 영상 파일 ──▶ 화재·연기 검출 ──▶ 마커 중심점 Homography ──▶ 화재 좌표 ──▶ 화면 표시
                            ▲
                    감지 제외 영역 (마우스 편집)
```

- 서버가 쓰는 검출 코드를 그대로 사용
- 영상 네 점을 클릭하는 좌표 지정 방식은 사용하지 않음 — 마커 기반으로 대체됨
- 감지 제외 영역은 좌표 보정과 별개 — 보정 방식을 바꿔도 그대로 사용 가능

### 로그 보는 법

```text
aruco configured=1 valid=1 fresh=1 markers=4/4 inlierCorners=4 rms=1.2px
```

| 항목 | 의미 |
| --- | --- |
| `configured=1` | 설정 파일과 해당 논리 채널을 읽음 |
| `markers=4/4` | 검출한 마커 4개가 모두 설정 파일에 등록돼 있음 |
| `inlierCorners=4` | 이름은 호환을 위해 유지되며, 현재 값은 채택된 마커 중심점 수 |
| `rms` | 영상에 다시 투영했을 때의 픽셀 오차 |
| `fresh=1` | 현재 사용 가능한 변환식이 있음 |
| `grid=(x,y)` | 화재 박스의 최종 60×60 표시 좌표 (0~59) |
| `factory=(x m,y m)` | 설정한 실제 공장 좌표 |
| `grid=invalid` | 마커 보정이 없거나, 불의 바닥점이 보드 밖 |

### 조작

| 입력 | 동작 |
| --- | --- |
| 왼쪽 클릭 | 꼭짓점 추가 |
| 오른쪽 클릭 | 마지막 꼭짓점 취소 |
| `R` | 현재 다각형을 감지 제외 영역으로 적용 |
| `U` | 마지막 제외 영역 삭제 |
| `C` | 모든 제외 영역 삭제 |
| `D` | 화재 분석 상세 표시 |
| `Space` | 일시정지 / 재생 |
| `Q` · `Esc` | 종료 |

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `DetectionDebugRunner.cpp` | 실행기 본체 — 프레임 수신 · 검출 호출 · 화면 표시 |
| `CMakeLists.txt` | 빌드 설정 · 모델과 DLL 복사 |
| `DetectionDebugRunner.sln` · `.vcxproj` | Visual Studio 프로젝트 |
| `DetectionDebugRunner.local.props.example` | 로컬 경로 설정 견본 |

---

## 🔧 빌드·실행

**필요** — Visual Studio · OpenCV · NCNN

Visual Studio 의 **폴더 열기**로 이 폴더를 열고 `DetectionDebugRunner` 를 `Release/x64` 로
빌드합니다. 또는 개발자 PowerShell 에서:

```powershell
cmake -S . -B out/build -A x64 `
  -DOpenCV_DIR=C:/opencv/build `
  -Dncnn_DIR=C:/ncnn/x64/lib/cmake/ncnn
cmake --build out/build --config Release
```

빌드가 끝나면 연기 모델과 `ncnn.dll` 이 실행 파일 옆으로 복사됩니다.

### 실행

```powershell
.\DetectionDebugRunner.exe "rtsp://사용자:비밀번호@카메라주소/스트림경로" --aruco-config .\aruco_board_config.txt --channel 2
```

저장된 보정 결과를 운영과 동일하게 시험하려면 렌즈 보정과 변환식 파일을 함께 지정합니다.

```powershell
.\DetectionDebugRunner.exe "rtsp://..." --headless --max-frames 120 --aruco-config .\aruco_board_config.txt --camera-calibration .\camera_calibration_ch3.yml --static-homography .\homography_ch3.yml --channel 3
```

영상 파일도 같은 방법으로 검사할 수 있습니다.

```powershell
.\DetectionDebugRunner.exe "C:\video\test.mp4" --aruco-config .\aruco_board_config.txt --channel 2
```

- `--channel` 은 카메라 장비의 물리 채널이 아니라 **설정 파일의 논리 채널** 번호입니다
  (현재 예제 설정은 채널 2)
- 설정 파일 경로는 `FIRE_ARUCO_CONFIG_PATH` 환경변수로도 지정할 수 있고,
  지정하지 않으면 실행 파일 옆의 `aruco_board_config.txt` 를 자동으로 읽습니다

---

## ✅ 해결한 문제

### 최종 박스만으로 오탐 원인을 확인하기 어려웠던 문제

운영 화면에는 최종 화재·연기 박스만 표시돼 색상, 움직임, 형태 점수 중 어느 단계에서
후보가 남거나 제거됐는지 확인하기 어려웠다. 운영 감지 코어를 그대로 호출하는
`DetectionDebugRunner`를 만들어 RTSP 카메라와 녹화 영상을 같은 조건으로 재현했다.
카메라 원본부터 색상·움직임·후보·형태학 처리·contour·최종 박스까지 중간 결과를
한 번에 저장할 수 있게 해, 파라미터 변경 전후를 동일 프레임으로 비교하고 발표용
파이프라인 이미지도 실제 실행 결과에서 추출할 수 있도록 해결했다.

---

## 🔗 참고

- 보정 도구 — [tools/calibration/README.md](../calibration/README.md)
- 상위 개요 — [detection/README.md](../../README.md)
