#ifndef TLSCLIENT_H
#define TLSCLIENT_H

#include <QObject>
#include <QSslSocket>
#include <QSslError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>

class TlsClient : public QObject
{
    Q_OBJECT // Qt의 시그널/슬롯 시스템을 사용하기 위한 필수 매크로

public:
    explicit TlsClient(QObject *parent = nullptr);
    ~TlsClient();

    // 서버에 접속하는 함수
    void connectToServer(const QString &host, quint16 port);
    
    // 서버와 연결을 끊는 함수
    void disconnectFromServer();

    // 서버로 제어 명령(데이터)을 보내는 함수
    void sendData(const QByteArray &data);

signals:
    // UI(mainwindow)로 상태를 전달하기 위한 시그널들 (허공에 외치는 신호)
    void connected();
    void disconnected();
    void dataReceived(const QByteArray &data);
    void errorOccurred(const QString &errorString);

    // 1. 영상 객체 감지 데이터
    void detectionReceived(int channel, int frameId, int srcW, int srcH, bool alarm, const QJsonArray &boxes);
    
    // 2. 센서 데이터
    void sensorReceived(const QString &zone, qint64 ts, double temp, double humidity, double gasPpm, double smokePpm, const QString &state);
    
    // 3. 전광판 상태 데이터
    void ledMatrixStatusReceived(int status);
    
    // 4. 액추에이터 상태 데이터
    void actuatorStatusReceived(int fan, int valve, int siren);

private slots:
    // QSslSocket의 내부 이벤트에 반응하는 슬롯들
    void onEncrypted();
    void onReadyRead();
    void onSslErrors(const QList<QSslError> &errors);

private:
    void parseJsonMessage(const QByteArray &jsonData); // JSON 파싱 전담 함수
    
    QSslSocket *m_sslSocket;
    QByteArray m_buffer; // TCP 조각화(Fragmentation)를 해결하기 위한 버퍼
};

#endif // TLSCLIENT_H