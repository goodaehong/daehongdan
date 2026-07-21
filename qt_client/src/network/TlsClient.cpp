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
    }
}

// --- 내부 슬롯 구현부 ---

void TlsClient::onEncrypted()
{
    emit connected(); // UI 쪽에 연결 성공을 알림
}

void TlsClient::onSslErrors(const QList<QSslError> &errors)
{
    m_sslSocket->ignoreSslErrors();
}

void TlsClient::onReadyRead()
{
    // 서버에서 날아온 패킷을 무조건 버퍼에 누적시킴
    m_buffer.append(m_sslSocket->readAll());

    // 버퍼 안에 줄바꿈(\n) 문자가 포함되어 있다면, 완전한 하나의 JSON 메시지가 도착했다는 뜻
    while (m_buffer.contains('\n')) {
        int newlineIndex = m_buffer.indexOf('\n');
        
        // \n 이전까지의 문자열을 잘라냄 (하나의 완벽한 JSON 문장)
        QByteArray jsonLine = m_buffer.left(newlineIndex).trimmed();
        
        // 처리한 문장은 버퍼에서 삭제 (\n 포함)
        m_buffer.remove(0, newlineIndex + 1);

        if (!jsonLine.isEmpty()) {
            emit dataReceived(jsonLine);
            parseJsonMessage(jsonLine);
        }
    }
}

void TlsClient::parseJsonMessage(const QByteArray &jsonData)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qDebug() << "JSON 파싱 에러:" << parseError.errorString() << "원본:" << jsonData;
        return;
    }

    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();
    
    // "type" 필드를 읽어서 어느 종류의 메시지인지 분류
    QString type = obj["type"].toString();

    if (type == "detection") {
        int channel = obj["channel"].toInt();
        int frameId = obj["frameId"].toInt();
        int srcW = obj["srcW"].toInt();
        int srcH = obj["srcH"].toInt();
        bool alarm = obj["alarm"].toBool();
        QJsonArray boxes = obj["boxes"].toArray();
        //qDebug() << "채널:" << channel << "프레임ID:" << frameId << "알람:" << alarm << "박스 수:" << boxes.size();
        emit detectionReceived(channel, frameId, srcW, srcH, alarm, boxes);
        
    } else if (type == "sensor") {
        QString zone = obj["zone"].toString();
        qint64 ts = obj["ts"].toVariant().toLongLong();
        double temp = obj["temp"].toDouble();
        double humidity = obj["humidity"].toDouble();
        double gasPpm = obj["gasPpm"].toDouble();
        double smokePpm = obj["smokePpm"].toDouble();
        QString state = obj["state"].toString();
        //qDebug() << "센서 데이터 - 구역:" << zone << "온도:" << temp << "습도:" << humidity << "가스(ppm):" << gasPpm << "연기(ppm):" << smokePpm << "상태:" << state;
        emit sensorReceived(zone, ts, temp, humidity, gasPpm, smokePpm, state);
        
    } else if (type == "led_matrix_status") {
        int status = obj["status"].toInt();
        //qDebug() << "전광판 상태 수신:" << status;
        emit ledMatrixStatusReceived(status);
        
    } else if (type == "actuator_status") {
        int fan = obj["fan"].toInt();
        int valve = obj["valve"].toInt();
        int siren = obj["siren"].toInt();
        //qDebug() << "액추에이터 상태 수신 - 팬:" << fan << "밸브:" << valve << "사이렌:" << siren;
        emit actuatorStatusReceived(fan, valve, siren);
        
    } else {
        qDebug() << "알 수 없는 메시지 타입 수신:" << type;
    }
}