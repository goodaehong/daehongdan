# streaming — MediaMTX 영상 재배포

## 📌 개요

- 카메라 4대의 RTSP 를 MediaMTX 로 받아 여러 곳에 재배포
- 서버 감지와 Qt 관제 화면이 같은 스트림을 공유

---

## ⚙️ 동작

```
한화 PNM-C16083RVQ ×4
        │  RTSP 720p 10fps
        ▼
    MediaMTX  (:8554)
    ┌────┴────┐
    ▼         ▼
서버 감지   관제 화면
```

**카메라에 직접 붙지 않는 이유** — 접속하는 곳이 늘어날수록 카메라 부하가 커집니다.
MediaMTX 가 카메라와 **4세션만 유지**하고, 서버·Qt·테스트 도구는 MediaMTX 에 붙습니다.
관제 화면을 여러 대 띄워도 카메라는 영향을 받지 않습니다.

**같은 스트림을 씁니다.** 감지와 표시가 다른 소스를 보면 감지 박스 좌표가 화면과 어긋납니다.

- 카메라 IP 는 `__CAM_IP__` 플레이스홀더 — `run-mediamtx.sh` 가 실행 시 치환
- 서버는 `rtsp://localhost:8554/cam1~4` 를 구독 (`RTSP_BASE_URL` 로 주입)

---

## 📁 주요 파일

| 파일 | 역할 |
| --- | --- |
| `mediamtx.yml` | 경로 정의 (`cam1`~`cam4`) · 포트 · 프로파일 |
| `run-mediamtx.sh` | IP 치환 후 MediaMTX 실행 |

---

## 🔧 빌드·실행

**필요** — MediaMTX 본체 (별도 설치, 저장소에 없음)

```bash
cd server/streaming
./run-mediamtx.sh
```

- 서버보다 **먼저** 띄워야 합니다. 안 그러면 카메라 연결이 실패합니다
- 포트 8554(RTSP) · 8000번대를 씁니다 — 이미 쓰는 프로세스가 있으면 기동 실패

---

## 🔗 참고

- 상위 개요 — [server/README.md](../README.md)
