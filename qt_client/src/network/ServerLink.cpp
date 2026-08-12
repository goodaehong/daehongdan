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

QString ServerLink::sendEmergencyTrigger(const QString &zone, const QString &cause, const QString &admin)
{
    QJsonObject extra;
    extra["cause"] = cause;
    return sendEmergencyRequest("emergency_trigger", "trigger", zone, admin, extra);
}

QString ServerLink::sendEmergencyClear(const QString &zone, const QString &admin, const QStringList &checklist)
{
    QJsonObject extra;
    extra["checklist"] = QJsonArray::fromStringList(checklist);
    return sendEmergencyRequest("emergency_clear", "clear", zone, admin, extra);
}

QString ServerLink::sendEmergencyRequest(const QString &type, const QString &mode, const QString &zone,
                                          const QString &admin, const QJsonObject &extraFields)
{
    const QString cmdId = generateCmdId();

    QJsonObject obj = extraFields;
    obj["type"] = type;
    obj["cmdId"] = cmdId;
    obj["zone"] = zone;
    obj["admin"] = admin;
    obj["ts"] = QDateTime::currentSecsSinceEpoch();
    sendLine(obj);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, cmdId, zone, mode]() {
        if (pendingEmergencyCommands.remove(cmdId) > 0)
            emit emergencyTimedOut(cmdId, zone, mode);
    });
    pendingEmergencyCommands.insert(cmdId, timer);
    timer->start(kControlTimeoutMs);

    return cmdId;
}

void ServerLink::sendWarningAck(const QString &zone, const QString &admin)
{
    QJsonObject obj;
    obj["type"] = "warning_ack";
    obj["zone"] = zone;
    obj["admin"] = admin;
    obj["ts"] = QDateTime::currentSecsSinceEpoch();
    sendLine(obj);
}

QString ServerLink::sendQuery(const QString &target, const QJsonObject &extraParams)
{
    const QString reqId = generateCmdId(); // cmdId와 동일한 채번기 재사용 (고유하기만 하면 됨)

    QJsonObject obj = extraParams;
    obj["type"] = "query";
    obj["reqId"] = reqId;
    obj["target"] = target;
    sendLine(obj);

    return reqId;
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
        // warnRemain은 warning 상태일 때만 서버가 채워 보냄. 없으면 -1로 "해당없음" 표시.
        const int warnRemain = obj.contains("warnRemain") ? obj.value("warnRemain").toInt() : -1;
        // 필드 자체가 없는 구버전 서버는 true(정상)로 취급 — 없다고 경고를 띄우면 안 됨.
        const bool responseOk = obj.contains("responseOk") ? obj.value("responseOk").toBool() : true;
        // clearCheck는 없으면 전부 false(=해제 불가) 취급 — 구버전 서버에서 체크리스트가 통과된 것처럼 보이면 안 됨.
        const QJsonObject clearCheck = obj.value("clearCheck").toObject();
        const bool clearSensor = clearCheck.value("sensor").toBool();
        const bool clearVision = clearCheck.value("vision").toBool();
        const bool clearActuator = clearCheck.value("actuator").toBool();
        // 필드 자체가 없는 구버전 서버는 true(정상)로 취급.
        const bool sensorOk = obj.contains("sensorOk") ? obj.value("sensorOk").toBool() : true;
        const bool dhtOk = obj.contains("dhtOk") ? obj.value("dhtOk").toBool() : true;
        // dangerSource 없으면(구버전 서버) "auto"로 취급 — 수동 발령이 아닌 게 안전한 기본값.
        const QString dangerSource = obj.contains("dangerSource") ? obj.value("dangerSource").toString() : "auto";
        const QString admin = obj.value("admin").toString();
        emit sensorReceived(obj.value("zone").toString(),
                             qint64(obj.value("ts").toDouble()),
                             obj.value("temp").toDouble(),
                             obj.value("humidity").toDouble(),
                             obj.value("gasPpm").toDouble(),
                             obj.value("smokePpm").toDouble(),
                             obj.value("flameVal").toDouble(),
                             obj.value("state").toString(),
                             obj.value("cause").toString(),
                             warnRemain, responseOk,
                             clearSensor, clearVision, clearActuator,
                             sensorOk, dhtOk, dangerSource, admin);
        // visionOk 없으면(구버전 서버) 4채널 다 true로 취급 — 없다고 오류 표시하면 안 됨.
        const QJsonArray visionArr = obj.value("visionOk").toArray();
        bool visionOk[4] = { true, true, true, true };
        for (int i = 0; i < 4 && i < visionArr.size(); ++i)
            visionOk[i] = visionArr.at(i).toBool();
        emit visionStatusReceived(visionOk[0], visionOk[1], visionOk[2], visionOk[3]);
    } else if (type == "led_matrix_status") {
        emit ledMatrixStatusReceived(obj.value("status").toInt());
    } else if (type == "actuator_status") {
        // link/fanSrc/valveSrc/sirenSrc는 서버가 추가하기로 한 필드라 구버전 서버에선 없을 수 있음
        // -> 없으면 빈 문자열(=미상).
        const int fan = obj.value("fan").toInt();
        const int valve = obj.value("valve").toInt();
        const int siren = obj.value("siren").toInt();
        // target 자체가 없는 구버전 서버는 현재값과 같다고 취급 — 미반영 경고가 잘못 뜨면 안 됨.
        const QJsonObject target = obj.value("target").toObject();
        const int targetFan = target.contains("fan") ? target.value("fan").toInt() : fan;
        const int targetValve = target.contains("valve") ? target.value("valve").toInt() : valve;
        const int targetSiren = target.contains("siren") ? target.value("siren").toInt() : siren;
        emit actuatorStatusReceived(fan, valve, siren,
                                     obj.value("link").toString(), obj.value("fanSrc").toString(),
                                     obj.value("valveSrc").toString(), obj.value("sirenSrc").toString(),
                                     targetFan, targetValve, targetSiren, obj.value("linkReason").toString());
    } else if (type == "control_ack") {
        const QString cmdId = obj.value("cmdId").toString();
        QTimer *timer = pendingCommands.take(cmdId);
        if (!timer)
            return; // 이미 처리됐거나(중복 응답) 타임아웃된 명령 -> 무시
        timer->stop();
        timer->deleteLater();
        emit controlResult(cmdId, obj.value("zone").toString(), obj.value("target").toString(),
                            obj.value("result").toString(), obj.value("reason").toString());
    } else if (type == "emergency_ack") {
        const QString cmdId = obj.value("cmdId").toString();
        QTimer *timer = pendingEmergencyCommands.take(cmdId);
        if (!timer)
            return; // 이미 처리됐거나(중복 응답) 타임아웃된 명령 -> 무시
        timer->stop();
        timer->deleteLater();
        // 거절 없음 — result는 항상 "accepted"
        emit emergencyResult(cmdId, obj.value("zone").toString(), obj.value("mode").toString(),
                              obj.value("result").toString());
    } else if (type == "query_result") {
        const QString reqId = obj.value("reqId").toString();
        const QString target = obj.value("target").toString();
        if (obj.value("result").toString() == "failed")
            emit queryFailed(reqId, obj.value("reason").toString());
        else
            emit queryResult(reqId, target, obj.value("rows").toArray());
    }
}
