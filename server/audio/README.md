# audio — 대피 안내 음성

## 📌 개요

- 위험 진입 시 대피 안내 음성 재생, 해제 시 정지
- 사이렌은 STM32 부저가 담당 — 이 모듈과 별개

---

## ⚙️ 동작

```
위험 진입 ──▶ SpeakerAlert_Start() ──▶ aplay 자식 프로세스 (반복 재생)
해제      ──▶ SpeakerAlert_Stop()  ──▶ 프로세스 종료
```

- `fork()` + `execlp("aplay", ...)` 로 별도 프로세스에서 재생 — 제어 루프를 막지 않음
- 재생이 끝나면 다시 실행해 반복
- 오디오 장치가 없어 `aplay` 가 즉시 실패하면 간격 없이 재시도돼 로그가 도배되므로,
  실패 시에는 대기 후 재시도

**ALSA 장치를 빌드 옵션으로 뺀 이유** — USB 사운드카드는 꽂는 순서에 따라 `hw` 번호가
바뀝니다. 소스에 박으면 장비마다 못 씁니다.

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `speaker_alert.cpp/h` | 재생 시작·정지·상태 |
| `evacuation_alert.wav` | 안내 음성 |

---

## 🔧 빌드·실행

**필요** — `alsa-utils` (`aplay`)

```bash
aplay -l                                    # 장치 번호 확인
cmake -S server -B server/build -DSPEAKER_ALSA_DEVICE=hw:2,0
```

- 기본값 `hw:2,0`
- 음성 파일 경로는 `AUDIO_FILE` 로 주입

---

## 🔗 참고

- 상위 개요 — [server/README.md](../README.md)
