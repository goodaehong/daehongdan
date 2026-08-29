# sensors — 센서 읽기 · 물리 단위 환산

## 📌 개요

- 커널 드라이버가 노출한 값을 읽어 물리 단위로 환산
- 온습도 · 가스 · 연기 · 불꽃 4종
- 하드웨어 없이 개발할 수 있도록 mock 제공

---

## ⚙️ 동작

### 읽는 값

| 센서 | 소자 | 단위 | 경로 |
| --- | --- | --- | --- |
| 온습도 | DHT22 | ℃ · % | GPIO 단일버스 |
| 가스 | MQ-9 (CO) | ppm | ADS1115 (I2C) |
| 연기 | MQ-2 | ppm | ADS1115 (I2C) |
| 불꽃 | DFR0076 | V | ADS1115 (I2C) |

### 실패를 다루는 방식

| 상황 | 처리 |
| --- | --- |
| 읽기 실패 | `false` 반환 — 값을 **0으로 채우지 않음** |
| DHT22 실패 | 온습도만 마지막 정상값 유지 (`dhtOk` 로 구분) |
| ADS1115 실패 | 전체 실패로 취급 — 위험 판단에 직결되므로 |
| 범위 밖 값 | 읽기 실패로 취급 |

- **0으로 채우면 「가스 0ppm = 안전」이 되어 위험이 저절로 풀림**
- DHT22 는 체크섬 오류가 잦아 가스·연기·불꽃까지 죽이지 않도록 분리
- 습도가 3000%대로 튀는 사례가 실측에서 확인돼 유효 범위 검사 추가

---

## 📁 주요 파일

| 파일 | 하는 일 |
| --- | --- |
| `sensor_reader.h` | 읽기 결과 구조 · 인터페이스 |
| `sensor_reader_hw.cpp` | 실물 — sysfs 읽기 · 유효 범위 검사 |
| `sensor_reader_mock.cpp` | mock — 하드웨어 없이 개발·시연 |
| `sensor_conversion.cpp/h` | ADC 원시값 → ppm · 전압 환산 |
| `test_sensor_reader.cpp` | 단독 읽기 테스트 |

---

## 🔧 빌드·실행

```bash
cmake -S server -B server/build -DUSE_MOCK_SENSOR=ON   # mock
cmake -S server -B server/build                        # 실물 (기본)
```

실물을 쓰려면 커널 드라이버가 먼저 로드돼 있어야 합니다
→ [drivers/README.md](../../drivers/README.md)

---

## 🔗 참고

- 판정 임계값 — [server/README.md](../README.md) 판정 기준
- 상위 개요 — [server/README.md](../README.md)
