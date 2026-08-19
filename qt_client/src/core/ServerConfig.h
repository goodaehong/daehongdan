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

// 서버가 실제로 TLS를 켰는지 여부. (2026-08-14 확인: main 브랜치 서버는 아직 평문 TCP만 지원 —
// server/net/link_plain.cpp가 ENABLE_TLS 옵션과 무관하게 항상 평문으로 동작하고, TLS 구현
// 파일(link_tls.cpp 등)은 아직 없음.) TLS 코드 자체는 이미 완성돼 있으니, 광렬님이 서버 측
// TLS를 실제로 띄우시면 이 값만 true로 바꾸면 된다 — Qt 쪽 재작업은 필요 없다.
constexpr bool kUseTls = false;

// 서버 TLS 인증서 고정(pinning)용 SHA-256 지문. 사내망 전용 라즈베리파이라 공인 CA 인증서가
// 아니라 자체서명(self-signed) 인증서를 쓰므로, "신뢰할 수 없는 발급자" 에러는 정상이다.
// 그렇다고 무조건 무시하면 TLS를 붙인 의미(중간자 공격 방지)가 없어지므로, 서버가 제시한 인증서의
// 지문이 여기 값과 일치할 때만 그 에러를 무시하도록 아래 verifyServerCertificate()가 검사한다.
//
// 광렬님이 서버 측 TLS(인증서 발급)를 완료하면, 그 인증서의 SHA-256 지문을 여기 채워 넣어야 한다.
// 빈 문자열인 동안은 개발 편의를 위해 인증서 검증 없이 연결하지만(콘솔에 경고를 남김),
// 이 값이 비어있는 채로 배포하면 안 된다.
inline const QString kServerCertSha256 = ""; // 예: "AB:CD:EF:...:12" 형식이든 공백 없는 hex든 무관(비교 시 정규화)

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
