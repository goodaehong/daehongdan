# TLS 서버 작업 현황 (feature/tls)

## 완료된 부분

- `TlsServer` 클래스로 리팩터링 (`main.cpp` → `TlsServer.cpp`/`TlsServer.h`)
- 인증서/키 로드(`server_cert.pem`, `server_private.pem`), TLS 핸드셰이크, 단일 접속(1:1) 정책
- 송신 전용 스레드 + 스레드 안전 큐 (`enqueueTxData`) — 외부 스레드에서 자유롭게 호출 가능
- 수신 루프 non-blocking 처리(`poll` 200ms 타임아웃)로 TX 스레드 starvation 문제 해결
- 수신 메시지 `\n` 기준 프레이밍 (TCP 스트림 분절 대응)
- 팀 합의 5개 항목(인증서 경로 / 수신 큐 구조 / 송신 동기화 / 포트 9999 / 단일 접속) 대조 검토 완료 — 전부 충족
- 구버전 잔재 파일(`main.cpp`, `server.crt`, 안 쓰는 `server.key`) 정리
- 라즈베리파이(`gwangbox`)에서 컴파일 확인 완료 (리팩터링 전 버전 기준)
- **`Link` 인터페이스 결합 완료** (`server/net/link_tls.cpp` 신설)
  - main 쪽에서 이미 `server_main.cpp`가 `stream_receiver.cpp`/`Sender` 대신 `Link&` 기반 아키텍처로 넘어가 있었음 — 그 인터페이스에 `TlsLink : public Link`로 얹는 방식으로 통합 (당초 계획이던 "stream_receiver.cpp 직접 결합"은 이미 낡은 전제였음)
  - `TlsServer::run()`을 `TlsLink::start()`에서 별도 스레드로 기동, `enqueueTxData`/`connected()`/`stop()`을 `Link`에 매핑
  - RX 큐를 파싱된 JSON(`control` 타입만 필터링)에서 **원문 라인 큐**(`waitAndPopRxLine`)로 변경 — 타입 판별 없이 다 큐잉하고, 라우팅은 `qt_link.cpp`의 `QtLink_RecvWorker`가 담당 (기존 `PlainLink` 경로와 동일한 방식). 기존 방식은 `control` 외 타입(`emergency`/`set_ignore_regions`/`set_floor_map`/`query`/`warning_ack`)이 전부 유실되는 문제가 있었음
  - `SSL_write` partial-write 대응(다 나갈 때까지 반복 전송), 수신 버퍼 8MB 상한 추가 — `link_plain.cpp`에 이미 적용된 견고성 패치와 동일 패턴
- **알려진 gap 정리 (라즈베리파이 없이 코드만으로 가능한 것 전부)**
  - `TlsServer::start()` 실패를 `false`로 알림 — 인증서/키 로드 실패해도 더 이상 `exit()`으로 프로세스를 죽이지 않음. `ready_` 플래그로 기록하고 `configureContext()`가 `bool` 반환, `TlsLink::start()`가 이걸 확인해서 `Link::start()` 실패 계약(`false` 반환)을 지키도록 함
  - `SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)` 추가 — TLS 1.2 미만 협상 차단
  - `SSL_CTX_check_private_key()` 추가 — 인증서/키 쌍이 안 맞으면 핸드셰이크 때가 아니라 시작 시점에 바로 실패
  - 연결을 서버가 스스로 끊는 모든 지점(정상 종료 감지, 8MB 초과, `stop()`, poll 오류)에 `SSL_shutdown()` 추가 — close_notify 한 번 시도 후 정리 (응답 대기는 안 함, 블로킹 리스크 최소화)
  - `poll()`이 `EINTR` 이외 오류로 실패하는 경로에 `ssl_`/`clientFd`/`connected_` 정리 추가 (기존엔 누락돼서 fd leak 가능성 있었음)

## 남은 과제

1. **Qt 클라이언트 측 TLS와 연동 테스트 미완료** — 장태호 님 클라이언트 TLS 작업 완료 후 실제 핸드셰이크 통합 테스트 필요. `link_tls.cpp` 결합 및 위 gap 수정들은 코드 리뷰 수준으로만 검증됐고, 라즈베리파이 빌드·실제 Qt 접속 테스트는 아직 안 함 (라즈베리파이 연결이 끊긴 상태에서 작업해서 컴파일 자체를 못 돌려봄 — 다음 파이 접속 시 최우선으로 빌드 확인 필요)
2. **인증서 경로(`server_cert.pem`/`server_private.pem`) 상대경로 하드코딩** — 실행 CWD에 의존. 다른 항목들과 달리 이건 라즈베리파이의 실제 배포 경로/관례를 모르는 채로 손대면 지금 되던 걸 깨뜨릴 위험이 있어 일부러 안 건드림. 실제 배포 시 인증서가 어디 놓이는지 확인 후 `FLOORMAP_IMAGE_PATH`처럼 `CMakeLists.txt` compile definition으로 절대경로화하는 걸 권장
