#ifndef SERVERLINK_H
#define SERVERLINK_H

#include <QObject>
#include <QByteArray>
#include <QVector>
#include <QMap>
#include "../core/DetectionTypes.h"

class QTcpSocket;
//class QSslSocket;
class QTimer;
class QJsonObject;

// 라즈베리파이 서버와의 JSON 소켓 통신 전담.
// 수신: detection(계약①), sensor(계약②), led_matrix_status, actuator_status, control_ack
// 송신: control(수동 제어 명령), false_alarm_report
class ServerLink : public QObject
{
    Q_OBJECT

public:
    explicit ServerLink(QObject *parent = nullptr);

    void connectToServer(const QString &host, quint16 port);

    // 반환값: cmdId (응답 매칭용, controlResult/controlTimedOut 시그널에서 다시 옴)
    QString sendControl(const QString &zone, const QString &target, const QString &action, const QString &admin);
    void sendFalseAlarmReport(int channel, int frameId, const QString &admin);

signals:
    void connectionStateChanged(bool connected);

    void detectionReceived(int channel, int frameId, int srcW, int srcH, bool alarm, const QVector<DetectionBox> &boxes);
    void sensorReceived(const QString &zone, qint64 ts, double temp, double humidity,
                         double gasPpm, double smokePpm, const QString &state);
    void ledMatrixStatusReceived(int status);
    void actuatorStatusReceived(int fan, int valve, int siren);

    void controlResult(const QString &cmdId, const QString &zone, const QString &target,
                        const QString &result, const QString &reason);
    void controlTimedOut(const QString &cmdId, const QString &zone, const QString &target);

private slots:
    void onReadyRead();

private:
    void handleLine(const QByteArray &line);
    void sendLine(const QJsonObject &obj);
    QString generateCmdId();

    QTcpSocket *socket;
    //QSslSocket *socket;
    QByteArray buffer;
    QMap<QString, QTimer *> pendingCommands;
    int cmdCounter = 0;
};

#endif // SERVERLINK_H
