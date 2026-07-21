#include "ServerLink.h"

#include <QTcpSocket>
//#include <QSslSocket>
//#include <QSslError> // 사설 인증서 에러 처리를 위해 추가
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace {
constexpr int kControlTimeoutMs = 3000;
}

ServerLink::ServerLink(QObject *parent)
    : QObject(parent)
{
    socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::readyRead, this, &ServerLink::onReadyRead);
    connect(socket, &QTcpSocket::connected, this, [this]() { emit connectionStateChanged(true); });
    connect(socket, &QTcpSocket::disconnected, this, [this]() { emit connectionStateChanged(false); });
    // socket = new QSslSocket(this);
    // connect(socket, &QSslSocket::readyRead, this, &ServerLink::onReadyRead);
    // // connected 대신 encrypted 사용: 암호화가 완료된 시점을 정확히 잡을 수 있음
    // connect(socket, &QSslSocket::encrypted, this, [this]() { emit connectionStateChanged(true); });
    // connect(socket, &QSslSocket::disconnected, this, [this]() { emit connectionStateChanged(false); });
    // // 사설 인증서(Self-signed) 에러 무시 로직 추가
    // connect(socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
    //     socket->ignoreSslErrors();
    // });
}

void ServerLink::connectToServer(const QString &host, quint16 port)
{
    socket->connectToHost(host, port);
    // // 일반 연결이 아닌 암호화 연결 함수 사용
    // socket->connectToHostEncrypted(host, port);
}

QString ServerLink::generateCmdId()
{
    return QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + QString::number(++cmdCounter, 36);
}

QString ServerLink::sendControl(const QString &zone, const QString &target, const QString &action, const QString &admin)
{
    const QString cmdId = generateCmdId();

    QJsonObject obj;
    obj["type"] = "control";
    obj["cmdId"] = cmdId;
    obj["zone"] = zone;
    obj["target"] = target;
    obj["action"] = action;
    obj["admin"] = admin;
    obj["ts"] = QDateTime::currentSecsSinceEpoch();
    sendLine(obj);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, cmdId, zone, target]() {
        if (pendingCommands.remove(cmdId) > 0)
            emit controlTimedOut(cmdId, zone, target);
    });
    pendingCommands.insert(cmdId, timer);
    timer->start(kControlTimeoutMs);

    return cmdId;
}

void ServerLink::sendFalseAlarmReport(int channel, int frameId, const QString &admin)
{
    QJsonObject obj;
    obj["type"] = "false_alarm_report";
    obj["channel"] = channel;
    obj["frameId"] = frameId;
    obj["admin"] = admin;
    sendLine(obj);
}

void ServerLink::sendLine(const QJsonObject &obj)
{
    const QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact) + "\n";
    socket->write(line);
}

void ServerLink::onReadyRead()
{
    buffer.append(socket->readAll());

    int newlineIndex;
    while ((newlineIndex = buffer.indexOf('\n')) != -1) {
        const QByteArray line = buffer.left(newlineIndex);
        buffer.remove(0, newlineIndex + 1);
        if (!line.trimmed().isEmpty())
            handleLine(line);
    }
}

void ServerLink::handleLine(const QByteArray &line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    const QString type = obj.value("type").toString();

    if (type == "detection") {
        QVector<DetectionBox> boxes;
        for (const QJsonValue &v : obj.value("boxes").toArray()) {
            const QJsonObject b = v.toObject();
            DetectionBox box;
            box.x = b.value("x").toInt();
            box.y = b.value("y").toInt();
            box.w = b.value("w").toInt();
            box.h = b.value("h").toInt();
            box.cls = b.value("cls").toString();
            box.score = b.value("score").toDouble();
            boxes.append(box);
        }
        emit detectionReceived(obj.value("channel").toInt(), obj.value("frameId").toInt(),
                                obj.value("srcW").toInt(), obj.value("srcH").toInt(),
                                obj.value("alarm").toBool(), boxes);
    } else if (type == "sensor") {
        emit sensorReceived(obj.value("zone").toString(),
                             qint64(obj.value("ts").toDouble()),
                             obj.value("temp").toDouble(),
                             obj.value("humidity").toDouble(),
                             obj.value("gasPpm").toDouble(),
                             obj.value("smokePpm").toDouble(),
                             obj.value("state").toString());
    } else if (type == "led_matrix_status") {
        emit ledMatrixStatusReceived(obj.value("status").toInt());
    } else if (type == "actuator_status") {
        emit actuatorStatusReceived(obj.value("fan").toInt(), obj.value("valve").toInt(), obj.value("siren").toInt());
    } else if (type == "control_ack") {
        const QString cmdId = obj.value("cmdId").toString();
        QTimer *timer = pendingCommands.take(cmdId);
        if (!timer)
            return; // 이미 처리됐거나(중복 응답) 타임아웃된 명령 -> 무시
        timer->stop();
        timer->deleteLater();
        emit controlResult(cmdId, obj.value("zone").toString(), obj.value("target").toString(),
                            obj.value("result").toString(), obj.value("reason").toString());
    }
}
