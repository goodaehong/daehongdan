#ifndef DETECTIONSTREAMCLIENT_H
#define DETECTIONSTREAMCLIENT_H

#include <QObject>
#include <QImage>

class QTcpSocket;

// fire_bridge_server(OpenCV/MSVC)가 보내는 오버레이 영상 프레임을 받는 클라이언트.
// 프로토콜: [4바이트 빅엔디안 JPEG 길이][JPEG 바이트][1바이트 알람 플래그]
class DetectionStreamClient : public QObject
{
    Q_OBJECT

public:
    explicit DetectionStreamClient(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);

signals:
    void frameReady(const QImage &frame, bool alarmActive);
    void connectionStateChanged(bool connected);

private slots:
    void onReadyRead();
    void onSocketError();

private:
    enum class ReadState { Length, Payload, AlarmFlag };

    QTcpSocket *socket;
    QByteArray buffer;
    ReadState state = ReadState::Length;
    quint32 expectedPayloadLen = 0;
    QByteArray pendingPayload;
};

#endif // DETECTIONSTREAMCLIENT_H
