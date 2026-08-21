#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QString>
#include <QSslSocket>
#include <QSslError>
#include <QSslCertificate>
#include <QCryptographicHash>
#include <QDebug>

// 서버 주소 단일 출처. LoginPage(연결 사전 확인)와 MainWindow(실제 데이터 소켓)가 각자 상수를
// 따로 들고 있다가 한쪽만 갱신되면서 어긋난 적이 있어(로그아웃 후 재로그인 시 "서버 연결 실패"
// 오탐 원인) 여기 하나로 합친다.
namespace ServerConfig {
inline const QString kServerHost = "172.20.35.185";
inline const quint16 kServerPort = 9999;

// 서버가 실제로 TLS를 켰는지 여부. (2026-08-21: PR #70 main 머지 완료 — server/net/tls_server.cpp
// + link_tls.cpp. 서버가 ENABLE_TLS=ON으로 빌드/실행 중일 때만 true여야 한다 — main 기본값은
// 여전히 OFF(server/CMakeLists.txt:61)라 평문 빌드로 뜬 서버에 이 값을 true로 켜면 "서버 연결
// 끊김"만 난다(전에 한 번 겪음). 연결 안 되면 먼저 파이 서버가 -DENABLE_TLS=ON으로 빌드된 게
// 맞는지부터 확인할 것.
constexpr bool kUseTls = true;

// 서버 TLS 인증서 고정(pinning)용 SHA-256 지문. 사내망 전용 라즈베리파이라 공인 CA 인증서가
// 아니라 자체서명(self-signed) 인증서를 쓰므로, "신뢰할 수 없는 발급자" 에러는 정상이다.
// 그렇다고 무조건 무시하면 TLS를 붙인 의미(중간자 공격 방지)가 없어지므로, 서버가 제시한 인증서의
// 지문이 여기 값과 일치할 때만 그 에러를 무시하도록 아래 verifyServerCertificate()가 검사한다.
//
// 아래 지문은 server/net/server_cert.pem(고정 경로, 절대경로화됨 — PR #70 리뷰 반영) 기준
// 테스트용 자체서명 인증서의 것이다. 인증서가 재발급되면 지문도 다시 뽑아야 한다:
// openssl x509 -in server/net/server_cert.pem -noout -fingerprint -sha256
inline const QString kServerCertSha256 =
    "86:A5:D8:17:CE:6E:98:0B:1D:71:BA:6A:61:E1:6B:1C:5E:43:27:F8:66:58:48:12:6A:46:AD:12:CE:9C:9B:8E";

// ServerLink(실제 데이터 소켓)와 LoginPage(로그인 시 사전 확인 소켓) 둘 다 같은 서버에 TLS로
// 붙으므로 인증서 검증 로직도 여기 한 곳에서 공유한다 — 따로 구현하면 한쪽만 고치고 잊어버리는
// 사고가 나기 쉽다(kServerHost가 그랬듯이). sslErrors 시그널 핸들러에서 그대로 호출하면 된다.
inline void verifyServerCertificate(QSslSocket *socket, const QList<QSslError> &errors)
{
    if (kServerCertSha256.isEmpty()) {
        qWarning() << "[TLS] ServerConfig::kServerCertSha256이 아직 비어있어 인증서 검증 없이 연결합니다."
                       " 배포 전 광렬님께 서버 인증서 지문을 받아 채워야 합니다. errors:" << errors;
        socket->ignoreSslErrors(errors);
        return;
    }

    const QSslCertificate cert = socket->peerCertificate();
    const QString fingerprint = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
    // 사용자가 콜론 구분("AB:CD:...")으로 붙여넣어도 그대로 비교되게 구분자를 없애고 비교한다.
    const QString expected = QString(kServerCertSha256).remove(':').trimmed();
    if (fingerprint.compare(expected, Qt::CaseInsensitive) == 0) {
        socket->ignoreSslErrors(errors);
    } else {
        qWarning() << "[TLS] 서버 인증서 지문 불일치 — 예상과 다른 서버입니다(중간자 공격 가능성)."
                       " 연결을 거부합니다. 받은 지문:" << fingerprint;
        // ignoreSslErrors()를 호출하지 않으면 QSslSocket이 알아서 연결을 끊는다.
    }
}
}

#endif // SERVERCONFIG_H
