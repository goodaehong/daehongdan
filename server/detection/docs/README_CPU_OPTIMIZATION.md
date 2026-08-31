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

## ✅ 해결한 문제

### 4채널 처리에서 CPU 사용량과 과거 프레임 지연이 커지던 문제

카메라 프레임을 들어오는 순서대로 모두 처리하면 분석 속도보다 입력 속도가 빠를 때
대기열이 쌓여 몇 초 전 화면을 뒤늦게 판단했다. 이를 해결하기 위해 채널별 대기
프레임을 가장 최근 프레임으로 덮어쓰고, 화재 분석을 640×360 해상도와 채널당
167ms 간격으로 제한했다. 연기 모델은 프로그램에서 한 번만 로드한 공유 worker가
채널별 최신 프레임만 처리하며, 2.5초보다 오래된 결과는 폐기하도록 변경했다.
OpenCV와 NCNN의 내부 스레드 수도 제한해 Raspberry Pi에서 중복 연산이 늘어나지
않도록 했다.

---

## 🔗 참고

- 감지 개요 — [../README.md](../README.md)
- 디버그 실행기 — [../tools/debug/README.md](../tools/debug/README.md)
