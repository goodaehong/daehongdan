# dts/README.md

# Device Tree Overlays

커스텀 커널 드라이버(`drivers/dht22`, `drivers/gas_sensor`)가 프로브될 수 있도록 하드웨어 연결 정보를 커널에 알려주는 DT overlay 모음.

## 구조

| 파일 | 대상 드라이버 | 버스 | compatible |
|---|---|---|---|
| `dht22-overlay.dts` | `drivers/dht22` | GPIO4 (BCM, 물리핀 7), 풀업 설정 포함 | `aosong,dht22` |
| `ads1115-overlay.dts` | `drivers/gas_sensor` | I2C1 (GPIO2/3, 물리핀 3/5), addr `0x48` | `ti,ads1115` |

## 적용 방법

보통은 각 드라이버 Makefile의 `make dt-apply`로 한 번에 처리됩니다 ([drivers/README.md](../drivers/README.md) 참고). 수동으로 하려면:

```bash
dtc -@ -I dts -O dtb -o dht22.dtbo dht22-overlay.dts
sudo cp dht22.dtbo /boot/firmware/overlays/
sudo dtoverlay dht22
```

`ads1115-overlay.dts`도 동일한 패턴(`dtc` → `cp` → `dtoverlay ads1115`)입니다.

해제:
```bash
sudo dtoverlay -r dht22
```

## 확인

```bash
dtoverlay -l   # 현재 적용된 오버레이 목록
dmesg | tail   # probe 로그 확인 (dht22 driver probed / ADS1115 probed at addr 0x48)
```

## 주의

오버레이는 **부팅 시 자동 적용되지 않습니다** — `dtoverlay` 명령은 그 부팅 세션에서만 유효하고, 재부팅하면 다시 `make dt-apply`를 실행해야 합니다. 영구 적용하려면 `/boot/firmware/config.txt`에 `dtoverlay=dht22`, `dtoverlay=ads1115` 줄을 추가해야 하는데, 아직 이 저장소 기준으로는 설정되어 있지 않습니다 (매 부팅마다 수동 적용 전제).

## 참고

- 각 오버레이의 `compatible` 문자열이 드라이버 소스의 `of_match_table`과 일치해야 프로브됩니다. `ads1115`는 mainline `ti_ads1015` 드라이버와 충돌하는 이슈가 있음 — [drivers/README.md 알려진 이슈](../drivers/README.md#알려진-이슈) 참고.
- 상위 문서: [drivers/README.md](../drivers/README.md)