# dts — Device Tree 오버레이

## 📌 개요

- 커스텀 커널 드라이버가 프로브될 수 있도록 **하드웨어 연결 정보를 커널에 알려주는** 오버레이
- 대상 — `drivers/dht22` · `drivers/gas_sensor`

---

## ⚙️ 동작

```
.dts ──▶ dtc 컴파일 ──▶ .dtbo ──▶ /boot/firmware/overlays/ ──▶ dtoverlay 적용
                                                              ──▶ 드라이버 probe
```

- 오버레이가 없으면 드라이버를 `insmod` 해도 **프로브가 안 되고 sysfs 노드도 안 생깁니다**
- `compatible` 문자열이 드라이버의 `of_match_table` 과 일치해야 매칭됩니다

---

## 📁 주요 파일

| 파일 | 대상 드라이버 | 버스 | compatible |
| --- | --- | --- | --- |
| `dht22-overlay.dts` | `drivers/dht22` | GPIO4 (BCM, 물리핀 7) · 풀업 포함 | `aosong,dht22` |
| `ads1115-overlay.dts` | `drivers/gas_sensor` | I2C1 (GPIO2/3, 물리핀 3/5) · addr `0x48` | `ti,ads1115` |

---

## 🔧 빌드·실행

보통은 각 드라이버 Makefile 의 `make dt-apply` 로 한 번에 처리합니다
→ [drivers/README.md](../drivers/README.md)

수동으로 하려면:

```bash
dtc -@ -I dts -O dtb -o dht22.dtbo dht22-overlay.dts
sudo cp dht22.dtbo /boot/firmware/overlays/
sudo dtoverlay dht22
```

확인:

```bash
dtoverlay -l   # 적용된 오버레이 목록
dmesg | tail   # dht22 driver probed / ADS1115 probed at addr 0x48
```

> ⚠️ 오버레이는 **부팅 시 자동 적용되지 않습니다.** `dtoverlay` 명령은 그 부팅 세션에서만
> 유효하고, 재부팅하면 다시 적용해야 합니다.
> 영구 적용하려면 `/boot/firmware/config.txt` 에 `dtoverlay=dht22` · `dtoverlay=ads1115` 를
> 추가해야 하는데, 이 저장소 기준으로는 설정되어 있지 않습니다 (매 부팅 수동 적용 전제).

---

## 🔗 참고

- 드라이버 빌드·로드 — [drivers/README.md](../drivers/README.md)
- 프로젝트 전체 개요 — [README.md](../README.md)
