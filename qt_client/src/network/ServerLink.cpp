#include "ServerLink.h"
#include "../core/ServerConfig.h"

#include <QSslSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace {
constexpr int kControlTimeoutMs = 3000;

// RoiRegion -> set_ignore_regions의 regions[] 원소. points는 [[x,y],...] (0~1 정규화).
QJsonArray roiRegionsToJson(const QVector<RoiRegion> &regions)
{
    QJsonArray arr;
    for (const RoiRegion &region : regions) {
        QJsonObject r;
        r["enabled"] = region.enabled;
        r["label"] = region.label;
        QJsonArray applyTo;
        if (region.applyFire) applyTo.append("fire");
        if (region.applySmoke) applyTo.append("smoke");
        r["applyTo"] = applyTo;
        QJsonArray points;
        for (const QPointF &p : region.points)
            points.append(QJsonArray{ p.x(), p.y() });
        r["points"] = points;
        arr.append(r);
    }
    return arr;
}

// query_result(target=ignore_regions)의 regions[] -> RoiRegion. 서버 필드명과 1:1 대응.
QVector<RoiRegion> roiRegionsFromJson(const QJsonArray &arr)
{
    QVector<RoiRegion> regions;
    for (const QJsonValue &v : arr) {
        const QJsonObject r = v.toObject();
        RoiRegion region;
        region.enabled = r.value("enabled").toBool(true);
        region.label = r.value("label").toString();

        // applyTo 필드 자체가 없으면(구버전 서버 등) 화재/연기 둘 다 적용으로 취급 — 서버 쪽
        // hasTarget()의 "필드 없으면 양쪽 다 적용" 규칙과 대칭을 맞춘다.
        const QJsonArray applyTo = r.value("applyTo").toArray();
        if (applyTo.isEmpty()) {
            region.applyFire = true;
            region.applySmoke = true;
        } else {
            region.applyFire = false;
            region.applySmoke = false;
            for (const QJsonValue &t : applyTo) {
                if (t.toString() == "fire") region.applyFire = true;
                if (t.toString() == "smoke") region.applySmoke = true;
            }
        }

        for (const QJsonValue &pv : r.value("points").toArray()) {
            const QJsonArray p = pv.toArray();
            if (p.size() == 2)
                region.points << QPointF(p[0].toDouble(), p[1].toDouble());
        }
        regions.append(region);
    }
    return regions;
}
}

ServerLink::ServerLink(QObject *parent)
    : QObject(parent)
{
    // QSslSocket은 QTcpSocket을 상속하므로 connectToHost()로 부르면 TLS 없이 평문 소켓처럼도
    // 동작한다 — 서버가 TLS를 켜기 전까지는 타입은 그대로 두고 호출 방식만 ServerConfig::kUseTls로
    // 가른다(광렬님 서버 TLS 완료되면 그 값만 true로 바꾸면 됨, 이 파일은 안 건드려도 된다).
    socket = new QSslSocket(this);
    connect(socket, &QSslSocket::readyRead, this, &ServerLink::onReadyRead);
    if (ServerConfig::kUseTls) {
        // connected 대신 encrypted를 쓴다 — connected는 TCP 핸드셰이크만 끝난 시점이라
        // TLS 협상이 안 끝난 상태에서 "연결됨"으로 잘못 표시하게 된다.
        connect(socket, &QSslSocket::encrypted, this, [this]() { emit connectionStateChanged(true); });
        connect(socket, &QSslSocket::sslErrors, this, &ServerLink::onSslErrors);
    } else {
        connect(socket, &QAbstractSocket::connected, this, [this]() { emit connectionStateChanged(true); });
    }
    connect(socket, &QSslSocket::disconnected, this, [this]() { emit connectionStateChanged(false); });
}

void ServerLink::connectToServer(const QString &host, quint16 port)
{
    if (ServerConfig::kUseTls)
        socket->connectToHostEncrypted(host, port);
    else
        socket->connectToHost(host, port);
}

void ServerLink::onSslErrors(const QList<QSslError> &errors)
{
    ServerConfig::verifyServerCertificate(socket, errors);
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

QString ServerLink::sendSetIgnoreRegions(int channel, const QVector<RoiRegion> &regions, double overlapThreshold)
{
    const QString cmdId = generateCmdId();

    QJsonObject obj;
    obj["type"] = "set_ignore_regions";
    obj["cmdId"] = cmdId;
    obj["channel"] = channel;
    obj["overlapThreshold"] = overlapThreshold;
    obj["regions"] = roiRegionsToJson(regions);
    sendLine(obj);

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
    } else if (type == "set_ignore_regions_ack") {
        emit ignoreRegionsAck(obj.value("cmdId").toString(), obj.value("channel").toInt(),
                               obj.value("result").toString() == "ok", obj.value("reason").toString());
    } else if (type == "query_result") {
        const QString reqId = obj.value("reqId").toString();
        const QString target = obj.value("target").toString();
        if (target == "ignore_regions") {
            // event_log/sensor_log와 달리 rows 배열로 안 오고 channel/regions가 바로 최상위에 있다
            // (접속 직후 push는 reqId 자체가 없음 — Qt는 push든 query 응답이든 구분 없이 반영한다).
            emit ignoreRegionsReceived(obj.value("channel").toInt(),
                                        obj.value("overlapThreshold").toDouble(0.5),
                                        roiRegionsFromJson(obj.value("regions").toArray()));
        } else if (obj.value("result").toString() == "failed") {
            emit queryFailed(reqId, obj.value("reason").toString());
        } else {
            emit queryResult(reqId, target, obj.value("rows").toArray());
        }
    }
}
