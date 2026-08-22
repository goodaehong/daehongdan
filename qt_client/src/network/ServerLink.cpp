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
// 이미지 저장 + EvacPlanner 변환까지 걸리는 작업이라 control보다 넉넉하게 잡는다.
constexpr int kFloorMapUploadTimeoutMs = 15000;

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

// floor_map_result / query_result(target=floor_map)의 bitmap[][] -> QVector<QVector<int>>.
// 실패 응답(gridSize·bitmap 없음)일 때도 그냥 빈 배열이 나오도록 값 검증 없이 그대로 옮긴다.
QVector<QVector<int>> floorMapBitmapFromJson(const QJsonArray &arr)
{
    QVector<QVector<int>> bitmap;
    for (const QJsonValue &rowV : arr) {
        QVector<int> row;
        for (const QJsonValue &cellV : rowV.toArray())
            row.append(cellV.toInt());
        bitmap.append(row);
    }
    return bitmap;
}

// displays[]/exits[] 공통 형태({"id","y","x"}) -> FloorMapMarker.
QVector<FloorMapMarker> floorMapMarkersFromJson(const QJsonArray &arr)
{
    QVector<FloorMapMarker> markers;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        FloorMapMarker m;
        m.id = o.value("id").toInt();
        m.y = o.value("y").toInt();
        m.x = o.value("x").toInt();
        markers.append(m);
    }
    return markers;
}

// routes[] -> FloorMapRoute. 서버는 waypoint를 {"y","x"}로 보내지만 FloorMapRoute::waypoints는
// Qt 좌표계 관례대로 QPoint(x,y)로 뒤집어 보관한다(FloorMapTypes.h 주석 참고).
QVector<FloorMapRoute> floorMapRoutesFromJson(const QJsonArray &arr)
{
    QVector<FloorMapRoute> routes;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        FloorMapRoute r;
        r.displayId = o.value("displayId").toInt();
        r.exitId = o.value("exitId").toInt();
        for (const QJsonValue &wv : o.value("waypoints").toArray()) {
            const QJsonObject w = wv.toObject();
            r.waypoints << QPoint(w.value("x").toInt(), w.value("y").toInt());
        }
        routes.append(r);
    }
    return routes;
}

// query_result(target=calib_status)의 channels[] -> CalibChannelStatus 벡터
// (server/calib/calib_store.cpp의 CalibStore_ToJson()과 1:1 대응, PR #65).
QVector<CalibChannelStatus> calibStatusListFromJson(const QJsonArray &arr)
{
    QVector<CalibChannelStatus> out;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        CalibChannelStatus s;
        s.channel = o.value("channel").toInt();
        s.stage = o.value("stage").toString();
        s.ready = o.value("ready").toBool();
        s.hint = o.value("hint").toString();
        s.detail = o.value("detail").toString();
        // 좌표 계산기가 도는 채널에서만 서버가 이 필드들을 아예 보낸다 — contains()로 구분.
        s.hasLive = o.contains("detectedMarkers");
        if (s.hasLive) {
            s.detectedMarkers = o.value("detectedMarkers").toInt();
            s.acceptedMarkers = o.value("acceptedMarkers").toInt();
            s.errorPx = o.value("errorPx").toDouble();
            s.lensApplied = o.value("lensApplied").toBool();
            s.staticHomography = o.value("staticHomography").toBool();
        }
        // running 필드가 없는 구버전 서버는 false(계산 중 아님)로 취급.
        s.running = o.value("running").toBool();
        out.append(s);
    }
    return out;
}

// ArucoChannelConfig -> set_aruco_config 본문(channel 제외 — 호출부가 따로 채움).
QJsonObject arucoConfigToJson(const ArucoChannelConfig &c)
{
    QJsonObject factory;
    factory["minX"] = c.factoryMinX; factory["minY"] = c.factoryMinY;
    factory["maxX"] = c.factoryMaxX; factory["maxY"] = c.factoryMaxY;

    QJsonObject board;
    board["minX"] = c.boardMinX; board["minY"] = c.boardMinY;
    board["maxX"] = c.boardMaxX; board["maxY"] = c.boardMaxY;

    QJsonArray markers;
    for (const ArucoMarkerConfig &m : c.markers) {
        QJsonObject mo;
        mo["id"] = m.id;
        mo["x"] = m.x;
        mo["y"] = m.y;
        mo["sizeCm"] = m.sizeCm;
        mo["rotation"] = m.rotation;
        markers.append(mo);
    }

    QJsonObject obj;
    obj["factory"] = factory;
    obj["modelScale"] = c.modelScale;
    obj["board"] = board;
    obj["markers"] = markers;
    return obj;
}

// query target=aruco_config 응답 -> ArucoChannelConfig. 필드가 통째로 없으면(설정 전) 빈 값 그대로.
ArucoChannelConfig arucoConfigFromJson(const QJsonObject &obj)
{
    ArucoChannelConfig c;
    c.channel = obj.value("channel").toInt();
    const QJsonObject factory = obj.value("factory").toObject();
    c.factoryMinX = factory.value("minX").toDouble();
    c.factoryMinY = factory.value("minY").toDouble();
    c.factoryMaxX = factory.value("maxX").toDouble();
    c.factoryMaxY = factory.value("maxY").toDouble();
    c.modelScale = obj.value("modelScale").toDouble();
    const QJsonObject board = obj.value("board").toObject();
    c.boardMinX = board.value("minX").toDouble();
    c.boardMinY = board.value("minY").toDouble();
    c.boardMaxX = board.value("maxX").toDouble();
    c.boardMaxY = board.value("maxY").toDouble();
    for (const QJsonValue &v : obj.value("markers").toArray()) {
        const QJsonObject mo = v.toObject();
        ArucoMarkerConfig m;
        m.id = mo.value("id").toInt();
        m.x = mo.value("x").toDouble();
        m.y = mo.value("y").toDouble();
        m.sizeCm = mo.value("sizeCm").toDouble();
        m.rotation = mo.value("rotation").toDouble();
        c.markers.append(m);
    }
    return c;
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

QString ServerLink::sendSetFloorMap(const QByteArray &pngBytes, const QString &fileName)
{
    const QString cmdId = generateCmdId();

    QJsonObject obj;
    obj["type"] = "set_floor_map";
    obj["cmdId"] = cmdId;
    obj["imageFormat"] = "png";
    obj["imageBase64"] = QString::fromLatin1(pngBytes.toBase64());
    obj["fileName"] = fileName;
    sendLine(obj);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, cmdId]() {
        if (pendingFloorMapUploads.remove(cmdId) > 0)
            emit floorMapUploadTimedOut(cmdId);
    });
    pendingFloorMapUploads.insert(cmdId, timer);
    timer->start(kFloorMapUploadTimeoutMs);

    return cmdId;
}

void ServerLink::sendReloadCalibration(int channel)
{
    // 서버 프로토콜에 cmdId가 없다(PR #65) — reload_calibration_result가 channel로만 옴.
    QJsonObject obj;
    obj["type"] = "reload_calibration";
    obj["channel"] = channel;
    sendLine(obj);
}

void ServerLink::sendSetArucoConfig(const ArucoChannelConfig &config)
{
    QJsonObject obj = arucoConfigToJson(config);
    obj["type"] = "set_aruco_config";
    obj["channel"] = config.channel;
    sendLine(obj);
}

QString ServerLink::sendQueryArucoConfig(int channel)
{
    QJsonObject params;
    params["channel"] = channel;
    return sendQuery("aruco_config", params);
}

void ServerLink::sendRunCalibration(int channel)
{
    QJsonObject obj;
    obj["type"] = "run_calibration";
    obj["channel"] = channel;
    sendLine(obj);
}

void ServerLink::sendCancelCalibration(int channel)
{
    QJsonObject obj;
    obj["type"] = "cancel_calibration";
    obj["channel"] = channel;
    sendLine(obj);
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
    } else if (type == "person") {
        QVector<DetectionBox> boxes;
        for (const QJsonValue &v : obj.value("boxes").toArray()) {
            const QJsonObject b = v.toObject();
            DetectionBox box;
            box.x = b.value("x").toInt();
            box.y = b.value("y").toInt();
            box.w = b.value("w").toInt();
            box.h = b.value("h").toInt();
            box.cls = "PERSON";   // 명세서엔 person boxes[] 원소에 cls가 없음 — 여기서 고정으로 채움
            box.score = b.value("score").toDouble();
            boxes.append(box);
        }
        emit personReceived(obj.value("channel").toInt(), obj.value("srcW").toInt(),
                             obj.value("srcH").toInt(), obj.value("count").toInt(), boxes);
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
        // voice 없는 구버전 서버는 false(=꺼짐)로 취급 — 없다고 아이콘이 잘못 켜지면 안 됨.
        const bool voice = obj.value("voice").toInt(0) != 0;
        emit actuatorStatusReceived(fan, valve, siren,
                                     obj.value("link").toString(), obj.value("fanSrc").toString(),
                                     obj.value("valveSrc").toString(), obj.value("sirenSrc").toString(),
                                     targetFan, targetValve, targetSiren, obj.value("linkReason").toString(),
                                     voice);
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
    } else if (type == "floor_map_result") {
        const QString cmdId = obj.value("cmdId").toString();
        QTimer *timer = pendingFloorMapUploads.take(cmdId);
        if (!timer)
            return; // 이미 타임아웃 처리됐거나(응답이 늦게 옴) 중복 응답 -> 무시
        timer->stop();
        timer->deleteLater();
        const bool ok = obj.value("result").toString() == "ok";
        emit floorMapUploadResult(cmdId, ok, obj.value("reason").toString(),
                                   obj.value("gridSize").toInt(),
                                   floorMapBitmapFromJson(obj.value("bitmap").toArray()),
                                   floorMapMarkersFromJson(obj.value("displays").toArray()),
                                   floorMapMarkersFromJson(obj.value("exits").toArray()),
                                   floorMapRoutesFromJson(obj.value("routes").toArray()),
                                   obj.value("fileName").toString(),
                                   qint64(obj.value("uploadedAt").toDouble()));
    } else if (type == "reload_calibration_result") {
        // cmdId가 없는 프로토콜 — channel로만 구분(PR #65). accepted일 뿐 완료 여부는 아님.
        emit calibReloadResult(obj.value("channel").toInt(),
                                obj.value("result").toString() == "accepted",
                                obj.value("reason").toString());
    } else if (type == "set_aruco_config_result") {
        emit arucoConfigResult(obj.value("channel").toInt(),
                                obj.value("result").toString() == "ok",
                                obj.value("reason").toString());
    } else if (type == "run_calibration_result") {
        emit runCalibrationResult(obj.value("channel").toInt(),
                                   obj.value("result").toString() == "accepted",
                                   obj.value("reason").toString());
    } else if (type == "cancel_calibration_result") {
        emit cancelCalibrationResult(obj.value("channel").toInt(),
                                      obj.value("result").toString() == "accepted",
                                      obj.value("reason").toString());
    } else if (type == "calibration_done") {
        // 서버가 요청 없이 먼저 보내는 알림 — ok/error/cancelled/timeout.
        emit calibrationDone(obj.value("channel").toInt(), obj.value("result").toString(),
                              obj.value("reason").toString());
    } else if (type == "query_result") {
        const QString reqId = obj.value("reqId").toString();
        const QString target = obj.value("target").toString();
        if (target == "calib_status") {
            emit calibStatusReceived(calibStatusListFromJson(obj.value("channels").toArray()));
        } else if (target == "aruco_config") {
            // 미설정 채널은 result:"empty"로 옴 — 그때는 필드 자체가 비어있으니 빈 구조체 그대로.
            const bool available = obj.value("result").toString() != "empty";
            emit arucoConfigReceived(obj.value("channel").toInt(), available, arucoConfigFromJson(obj));
        } else if (target == "ignore_regions") {
            // event_log/sensor_log와 달리 rows 배열로 안 오고 channel/regions가 바로 최상위에 있다
            // (접속 직후 push는 reqId 자체가 없음 — Qt는 push든 query 응답이든 구분 없이 반영한다).
            emit ignoreRegionsReceived(obj.value("channel").toInt(),
                                        obj.value("overlapThreshold").toDouble(0.5),
                                        roiRegionsFromJson(obj.value("regions").toArray()));
        } else if (target == "floor_map") {
            // 저장된 결과가 없으면 result:"empty" (gridSize·bitmap 등 필드 자체가 없음).
            const bool available = obj.value("result").toString() == "ok";
            emit floorMapReceived(available, obj.value("gridSize").toInt(),
                                   floorMapBitmapFromJson(obj.value("bitmap").toArray()),
                                   floorMapMarkersFromJson(obj.value("displays").toArray()),
                                   floorMapMarkersFromJson(obj.value("exits").toArray()),
                                   floorMapRoutesFromJson(obj.value("routes").toArray()),
                                   obj.value("fileName").toString(),
                                   qint64(obj.value("uploadedAt").toDouble()));
        } else if (target == "clip") {
            // result: "ok"/"empty"/"error". data는 mp4 원본을 base64로 실은 것 — ok일 때만 채워짐.
            const QString result = obj.value("result").toString();
            const QByteArray data = (result == "ok")
                ? QByteArray::fromBase64(obj.value("data").toString().toLatin1())
                : QByteArray();
            emit clipReceived(reqId, result, data);
        } else if (target == "snapshot") {
            // clip과 동일한 형태(ok/empty/error), jpg 원본 바이트(PR #69).
            const QString result = obj.value("result").toString();
            const QByteArray data = (result == "ok")
                ? QByteArray::fromBase64(obj.value("data").toString().toLatin1())
                : QByteArray();
            emit snapshotReceived(reqId, result, data);
        } else if (obj.value("result").toString() == "failed") {
            emit queryFailed(reqId, obj.value("reason").toString());
        } else {
            emit queryResult(reqId, target, obj.value("rows").toArray());
        }
    }
}
