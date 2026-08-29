# sensors — 센서 읽기 · 물리 단위 환산

## 📌 개요

- 커널 드라이버가 노출한 sysfs 값을 읽어 물리 단위로 환산
- 온습도(DHT22) · 가스(MQ-9) · 연기(MQ-2) · 불꽃(DFR0076) 4종
- mock / 실물을 빌드 옵션으로 교체

---

## ⚙️ 동작

```
sysfs ──▶ SensorReader_Read() ──▶ SensorReading (℃ · % · ppm · V)
```

**읽기 실패를 0으로 채우지 않습니다.** 「가스 0ppm = 안전」이 되어 위험이 저절로 풀리기
때문입니다. 실패 시 `false` 만 돌려주고, 값 유지·신선도 판정은 호출부(`sensorWorker`)가 합니다.

**DHT22 와 ADS1115 를 분리해 처리합니다.**

- DHT22 는 체크섬 오류가 잦음 → 실패해도 가스·연기·불꽃까지 죽이지 않음
- 반환값은 ADS1115(위험 판단에 직결) 성공 여부로만 결정
- DHT22 실패 시 온습도는 마지막 정상값 유지 (`dhtOk` 로 구분)

**물리적으로 불가능한 값도 걸러냅니다.** 체크섬을 통과했는데 습도가 3000%대로 튀는 사례가
실측에서 확인됐습니다. 범위 밖이면 「읽기 실패」로 취급합니다.

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `sensor_reader.h` | `SensorReading` 구조체 · 인터페이스 |
| `sensor_reader_hw.cpp` | 실물 — sysfs 읽기 · 유효 범위 검사 |
| `sensor_reader_mock.cpp` | mock — 하드웨어 없이 개발·시연 |
| `sensor_conversion.cpp/h` | raw ADC → ppm · 전압 환산 |
| `test_sensor_reader.cpp` | 단독 읽기 테스트 |

---

## 🔧 빌드·실행

```bash
cmake -S server -B server/build -DUSE_MOCK_SENSOR=ON   # mock
cmake -S server -B server/build                        # 실물 (기본)
```

- 실물은 커널 드라이버가 먼저 로드돼 있어야 합니다 → [drivers/README.md](../../drivers/README.md)

---

## 🔗 참고

- 임계값·판정 기준 — [judgement.h](../judgement.h) · 통신 명세서
- 상위 개요 — [server/README.md](../README.md)
