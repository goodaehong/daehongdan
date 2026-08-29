# 화재 감지 CPU 최적화

## 📌 개요

- Raspberry Pi 4에서 4채널 화재 감지를 채널당 약 6fps로 처리하기 위한 최적화 기록
- 현재 화재 분석 입력은 640×360이며 최종 기준값은 `AppConfig.h`를 따름

---

화재 색상·백색 코어·움직임·형태 분석을 유지하면서 반복 계산과 메모리 할당을 줄입니다.

## ⚙️ 동작

1. HSV and BGR are split once per detection and reused.
2. The white-core mask is built once and reused.
3. 3x3, 5x5 and 9x9 morphology kernels are cached.
4. MOG2 runs on half-resolution grayscale; full-resolution absdiff remains enabled.
5. Previous-frame buffers use copyTo() to reuse allocation.
6. Contour-local point arrays are no longer copied; drawContours uses an offset.
7. Expensive contour inspection is capped at the 10 largest candidates.
8. Debug image generation is disabled by default.

### 예상되는 절충

MOG2를 절반 해상도로 처리하므로 아주 작은 움직임 반응은 약해질 수 있습니다. 이를 보완하기 위해 640×360 프레임 차분, 화재 색상, 백색 코어와 후광 분석은 유지합니다. 후보가 10개를 넘는 복잡한 장면에서는 작은 불꽃을 놓칠 수 있으므로 실제 영상으로 확인한 뒤 `MAX_CONTOURS_TO_ANALYZE`를 조정합니다.

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `AppConfig.h` | 분석 해상도·주기·후보 상한 설정 |
| `FlameDetector.cpp/h` | 색상·움직임·형태 분석과 후보 필터링 |
| `FireDetectionRuntime.cpp/h` | 채널별 제출·결과·좌표 처리 |

---

## 🔧 빌드·실행

Linux/Raspberry Pi:

```bash
cmake -S server -B server/build -DCMAKE_BUILD_TYPE=Release
cmake --build server/build -j4
```

Windows에서 감지 디버그 실행기를 빌드하려면:

```powershell
cmake -S server/detection/tools/debug -B server/detection/tools/debug/build
cmake --build server/detection/tools/debug/build --config Release
```

변경 전후에는 같은 영상과 같은 프레임 구간으로 검출 수·지연·CPU 사용률을 비교합니다.

---

## 🛠 문제 해결

| 증상 | 확인할 것 |
| --- | --- |
| 작은 불꽃을 놓침 | `MAX_CONTOURS_TO_ANALYZE`와 작은 후보 면적 기준 확인 |
| CPU 사용률이 높음 | 입력이 640×360인지와 채널당 167ms 제한이 적용됐는지 확인 |
| 최적화 후 결과가 달라짐 | 같은 영상·프레임 수·ROI 설정으로 전후 결과 비교 |

---

## 🔗 참고

- 감지 개요 — [../README.md](../README.md)
- 디버그 실행기 — [../tools/debug/README.md](../tools/debug/README.md)
