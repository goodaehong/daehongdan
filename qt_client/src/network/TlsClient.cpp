#include "TlsClient.h"
#include <QDebug>

TlsClient::TlsClient(QObject *parent)
    : QObject(parent), m_sslSocket(new QSslSocket(this))
{
    // 소켓의 상태 변화 시그널을 이 클래스의 슬롯과 연결
    connect(m_sslSocket, &QSslSocket::encrypted, this, &TlsClient::onEncrypted);
    connect(m_sslSocket, &QSslSocket::readyRead, this, &TlsClient::onReadyRead);
    connect(m_sslSocket, &QSslSocket::disconnected, this, &TlsClient::disconnected);
    
    // 사설 인증서 에러 처리를 위한 연결
    connect(m_sslSocket, QOverload<const QList<QSslError> &>::of(&QSslSocket::sslErrors),
            this, &TlsClient::onSslErrors);
}

TlsClient::~TlsClient()
{
    disconnectFromServer();
}

void TlsClient::connectToServer(const QString &host, quint16 port)
{
    qDebug() << "TLS 서버에 접속 시도 중...:" << host << port;
    m_sslSocket->connectToHostEncrypted(host, port);
}

void TlsClient::disconnectFromServer()
{
    if (m_sslSocket->isOpen()) {
        m_sslSocket->close();
    }
}

void TlsClient::sendData(const QByteArray &data)
{
    if (m_sslSocket->isEncrypted()) {
        m_sslSocket->write(data);
        m_sslSocket->flush();
    } else {
        qDebug() << "에러: 암호화된 연결이 아닙니다. 데이터를 보낼 수 없습니다.";
    }
}

// --- 내부 슬롯 구현부 ---

void TlsClient::onEncrypted()
{
    qDebug() << "TLS 핸드셰이크 성공! 안전한 통신이 준비되었습니다.";
    emit connected(); // UI 쪽에 연결 성공을 알림
}

void TlsClient::onReadyRead()
{
    // 서버로부터 데이터가 들어오면 모두 읽어서 UI 쪽으로 전달
    QByteArray data = m_sslSocket->readAll();
    emit dataReceived(data);
}

void TlsClient::onSslErrors(const QList<QSslError> &errors)
{
    for (const QSslError &error : errors) {
        qDebug() << "SSL 인증서 경고:" << error.errorString();
    }
    
    // 라즈베리파이에서 만든 '사설 인증서'를 허용하도록 강제 설정
    m_sslSocket->ignoreSslErrors();
}