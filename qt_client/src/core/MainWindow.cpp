#include "MainWindow.h"
#include "../pages/MonitorPage.h"
#include "../pages/EventLogPage.h"
#include "../pages/GraphPage.h"
#include "../pages/HelpPage.h"
#include "../pages/FloorMapPage.h"
#include "../widgets/ArucoCalibrationDialog.h"
#include "../network/ServerLink.h"
#include "../widgets/WarningAlertDialog.h"
#include "../widgets/DangerGlowOverlay.h"
#include "ServerConfig.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QStackedWidget>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QResizeEvent>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QGraphicsDropShadowEffect>
#include <QColor>

namespace {
const QString kMediaMtxHost = "172.20.35.185"; // MediaMTX가 도는 라즈베리파이 주소 (카메라 IP 아님)
// 감지/센서/제어 JSON 소켓 주소는 ServerConfig.h(ServerConfig::kServerHost/kServerPort)로 옮김 —
// LoginPage.cpp가 따로 들고 있던 사본과 어긋나면서 "서버 켜져 있는데 로그인 화면에서 연결 실패로
// 뜨는" 버그가 났던 적이 있어, 두 파일이 같은 상수를 참조하도록 단일화한다.

const QStringList kTabNames = { "모니터링", "이벤트로그", "그래프", "평면도", "도움말" };

const QString kBg = "#0a0a12";
const QString kCardBg = "#14141f";   // 버튼 등 배경과 살짝 구분되는 카드 톤 (다른 화면들과 동일)
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";
const QString kAccentDark = "#6a5cd6";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("SafeVision - 지능형 통합 화재 관제 시스템");
    resize(1400, 860);

    // "구역" 명칭이 "공장"으로 바뀌고 A~D 4개 공장 체제가 됨. 실제 센서/카메라 하드웨어는 A공장에만
    // 있고(대피 모드 논의 때 확정된 내용) B/C/D공장은 기존 B구역처럼 DEMO 시뮬레이션으로 표시된다.
    zones.append({ "A공장", ZoneState::Warning, 24.3, 42.0 });
    zones.append({ "B공장", ZoneState::Safe, 23.1, 38.0 });
    zones.append({ "C공장", ZoneState::Safe, 22.8, 40.0 });
    zones.append({ "D공장", ZoneState::Safe, 25.0, 45.0 });

    centralArea = new QWidget(this);
    centralArea->setStyleSheet(QString("background-color:%1;").arg(kBg));
    auto *rootLayout = new QVBoxLayout(centralArea);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(createDangerBanner());
    rootLayout->addWidget(createTopBar());
    rootLayout->addWidget(createSubTabBar());
    createWarningAlertArea();

    dangerGlow = new DangerGlowOverlay(this);

    monitorPage = new MonitorPage(centralArea);
    eventLogPage = new EventLogPage(centralArea);
    graphPage = new GraphPage(centralArea);
    floorMapPage = new FloorMapPage(centralArea);
    helpPage = new HelpPage(centralArea);

    connect(monitorPage, &MonitorPage::demoStateRequested, this, [this](ZoneState state) {
        setZoneState(currentZone, state);
    });

    serverLink = new ServerLink(this);

    connect(graphPage, &GraphPage::sensorLogRequested, this,
            [this](const QString &zone, qint64 from, qint64 to) {
                QJsonObject params;
                params["from"] = from;
                params["to"] = to;
                params["zone"] = zone;
                pendingGraphReqId = serverLink->sendQuery("sensor_log", params);
            });

    connect(monitorPage, &MonitorPage::controlActionRequested, this,
            [this](const QString &target, const QString &action, const QString &title) {
                const QString zoneName = zones[currentZone].name;
                const QString zoneId = zoneName.left(1); // "A구역" -> "A"

                const QString cmdId = serverLink->sendControl(zoneId, target, action, "admin");
                pendingControlTitles.insert(cmdId, title);
                monitorPage->showControlStatus(QString("처리 중... (%1)").arg(title), "#8d87a0");
                // 값을 낙관적으로 미리 반영하지 않는다 — 그러면 방금 띄운 "처리 중..."이 바로 덮여
                // 안 보인다. 실제 값은 서버의 actuator_status(명령 직후 자동 전송)가 확정해준다.
                monitorPage->setActuatorRowStatus(target, "처리 중...", "#8d87a0");
            });

    // 비상 모드는 별도 메시지 타입(emergency_trigger/clear)이라 sendControl()과 분리해서 보낸다.
    // zone은 "발생 구역 표시용"일 뿐 실제 적용은 서버가 전 구역에 한다.
    // TODO(emergency-mode #8/#10/#11): 원인 선택 모달·체크리스트 모달·로그인 실명이 아직 없어서
    // 지금은 임시로 기본값(cause="fire_confirmed", checklist 빈 배열, admin="admin")을 보낸다.
    connect(monitorPage, &MonitorPage::emergencyTriggerRequested, this,
            [this](const QString &cause) {
                const QString zoneId = zones[currentZone].name.left(1);
                serverLink->sendEmergencyTrigger(zoneId, cause, "admin");
                monitorPage->showControlStatus("처리 중... (위험 모드 전환)", "#8d87a0");
            });
    connect(monitorPage, &MonitorPage::emergencyClearRequested, this,
            [this](const QString &admin, const QStringList &checklist) {
                const QString zoneId = zones[currentZone].name.left(1);
                serverLink->sendEmergencyClear(zoneId, admin, checklist);
                monitorPage->showControlStatus("처리 중... (위험 모드 해제)", "#8d87a0");
            });

    connect(serverLink, &ServerLink::emergencyResult, this,
            [this](const QString &, const QString &, const QString &mode, const QString &result) {
                Q_UNUSED(result);   // 거절 없음 — 항상 "accepted"
                const QString label = mode == "trigger" ? "위험 모드 전환" : "위험 모드 해제";
                monitorPage->showControlStatus(QString("완료: %1").arg(label), "#34d399");
                // 버튼/배너 상태는 여기서 낙관적으로 바꾸지 않는다 -> sensor 메시지의
                // state/dangerSource가 실제 상태를 확정해서 알려준다 (updateDangerIndicators에서 반영).
                eventLogPage->requestRefresh(); // 서버가 이 응답 전에 이미 event_log를 남겨둔 상태
            });
    connect(serverLink, &ServerLink::emergencyTimedOut, this,
            [this](const QString &, const QString &, const QString &) {
                monitorPage->showControlStatus("응답 없음 — 서버 연결 확인 필요", "#f87171");
            });

    // ROI(감시 제외 영역) 편집이 끝나면 그 채널만 서버로 전송. overlapThreshold는 서버가 전역
    // 0.5로 고정 운용하기로 해서(PR #49 회신) sendSetIgnoreRegions 기본값 그대로 둔다.
    connect(monitorPage, &MonitorPage::roiRegionsChanged, this,
            [this](int channel, const QVector<RoiRegion> &regions) {
                serverLink->sendSetIgnoreRegions(channel, regions);
                monitorPage->showControlStatus(QString("처리 중... (Ch.%1 감시 제외 영역 저장)").arg(channel), "#8d87a0");
            });
    connect(serverLink, &ServerLink::ignoreRegionsAck, this,
            [this](const QString &, int channel, bool ok, const QString &reason) {
                if (ok) {
                    monitorPage->showControlStatus(QString("완료: Ch.%1 감시 제외 영역 저장").arg(channel), "#34d399");
                } else {
                    monitorPage->showControlStatus(
                        QString("실패: Ch.%1 감시 제외 영역 (%2)").arg(channel)
                            .arg(reason.isEmpty() ? "알 수 없는 오류" : reason), "#f87171");
                }
            });
    // 접속 직후 서버가 4채널 전부 push하거나(재접속 시 자동), query 응답으로 온 것 — 둘 다
    // 여기서 그대로 해당 채널 카메라 카드에 반영한다.
    connect(serverLink, &ServerLink::ignoreRegionsReceived, this,
            [this](int channel, double /*overlapThreshold*/, const QVector<RoiRegion> &regions) {
                monitorPage->applyRoiRegionsFromServer(channel, regions);
            });

    // 평면도 "서버로 전송 및 적용" -> set_floor_map. 결과/타임아웃은 FloorMapPage 다이얼로그가
    // 직접 받아 처리하므로(자체 완결형 UI) MainWindow는 그냥 중계만 한다.
    connect(floorMapPage, &FloorMapPage::floorMapUploadRequested, this,
            [this](const QByteArray &pngBytes) { serverLink->sendSetFloorMap(pngBytes); });
    // floorMapUploadResult는 cmdId가 맨 앞에 있는데(ROI ack와 같은 패턴) FloorMapPage는 업로드를
    // 한 번에 하나만 진행시켜 cmdId 매칭이 필요 없으므로 여기서 버리고 나머지만 넘긴다.
    connect(serverLink, &ServerLink::floorMapUploadResult, this,
            [this](const QString &, bool ok, const QString &reason, int gridSize,
                   const QVector<QVector<int>> &bitmap, const QVector<FloorMapMarker> &displays,
                   const QVector<FloorMapMarker> &exits, const QVector<FloorMapRoute> &routes) {
                floorMapPage->onUploadResult(ok, reason, gridSize, bitmap, displays, exits, routes);
            });
    connect(serverLink, &ServerLink::floorMapUploadTimedOut, floorMapPage, &FloorMapPage::onUploadTimedOut);
    // 접속 직후 query target=floor_map 응답 — 서버에 저장된 결과가 있으면(available) 그대로 반영.
    // 없으면(신규 설치 등) 아무것도 안 하고 "미등록" 안내 배너를 그대로 둔다.
    connect(serverLink, &ServerLink::floorMapReceived, this,
            [this](bool available, int gridSize, const QVector<QVector<int>> &bitmap,
                   const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                   const QVector<FloorMapRoute> &routes) {
                if (available)
                    floorMapPage->applyServerData(gridSize, bitmap, displays, exits, routes);
            });

    // 이벤트로그 자체는 서버가 control 처리 시 db.insertEvent()로 이미 남기므로 Qt가 중복으로
    // 만들지 않는다. 다만 명세서(3-3)대로 성공/실패/타임아웃 각각 화면에 즉시 알림은 띄워야 한다.
    connect(serverLink, &ServerLink::controlResult, this,
            [this](const QString &cmdId, const QString &, const QString &target,
                   const QString &result, const QString &reason) {
                const QString title = pendingControlTitles.take(cmdId);
                if (result == "ok") {
                    monitorPage->showControlStatus(QString("완료: %1").arg(title.isEmpty() ? "명령" : title), "#34d399");
                    // 성공 시엔 서버가 명령 직후 actuator_status를 다시 보내주므로 곧 실제 값으로 덮인다.
                } else {
                    const QString reasonText = reason.isEmpty() ? "알 수 없는 오류" : reason;
                    monitorPage->showControlStatus(
                        QString("실패: %1 (%2)").arg(title.isEmpty() ? "명령" : title, reasonText), "#f87171");
                    monitorPage->setActuatorRowStatus(target, "실패", "#f87171");
                }
                // 서버는 성공/실패 상관없이 매 수동 제어를 event_log에 남긴다 — 즉시 반영.
                eventLogPage->requestRefresh();
            });
    connect(serverLink, &ServerLink::controlTimedOut, this,
            [this](const QString &cmdId, const QString &, const QString &target) {
                pendingControlTitles.remove(cmdId);
                monitorPage->showControlStatus("응답 없음 — 서버 연결 확인 필요", "#f87171");
                monitorPage->setActuatorRowStatus(target, "응답 없음", "#f87171");
            });

    connect(serverLink, &ServerLink::actuatorStatusReceived, this,
            [this](int fan, int valve, int siren, const QString &link,
                   const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc,
                   int targetFan, int targetValve, int targetSiren, const QString &linkReason) {
                currentFan = fan;
                currentValve = valve;
                currentSiren = siren;
                monitorPage->setActuatorStatus(fan, valve, siren, link, fanSrc, valveSrc, sirenSrc,
                                                targetFan, targetValve, targetSiren, linkReason);
            });

    connect(serverLink, &ServerLink::visionStatusReceived, this,
            [this](bool ch1, bool ch2, bool ch3, bool ch4) {
                monitorPage->setCameraVisionStatus(ch1, ch2, ch3, ch4);
            });

    connect(serverLink, &ServerLink::detectionReceived, this,
            [this](int channel, int, int srcW, int srcH, bool alarm, const QVector<DetectionBox> &boxes) {
                monitorPage->updateDetection(channel, srcW, srcH, boxes);
                monitorPage->setChannelAlarm(channel, alarm);
            });

    // 사람 감지(명세서 3번 계약) — 판단(state)엔 영향 없이 카메라 카드 배지/오버레이만 갱신.
    connect(serverLink, &ServerLink::personReceived, this,
            [this](int channel, int srcW, int srcH, int count, const QVector<DetectionBox> &boxes) {
                monitorPage->updatePersonBoxes(channel, srcW, srcH, count, boxes);
            });

    connect(serverLink, &ServerLink::sensorReceived, this,
            [this](const QString &zoneId, qint64, double temp, double humidity,
                   double gasPpm, double smokePpm, double flameVal, const QString &state,
                   const QString &cause, int warnRemain, bool responseOk,
                   bool clearSensor, bool clearVision, bool clearActuator,
                   bool sensorOk, bool dhtOk, const QString &dangerSource, const QString &admin) {
                // sensor 메시지가 살아있다는 증거 — 워치독(emergency-mode #19)이 이 시각을 기준으로 판단.
                const bool wasStale = lastSensorMsgAt.isValid() && lastSensorMsgAt.msecsTo(QDateTime::currentDateTime()) > 5000;
                lastSensorMsgAt = QDateTime::currentDateTime();
                if (wasStale)
                    refreshConnBadge(); // 끊겼다가 막 복구된 순간이면 배지 즉시 갱신 (5초 기다릴 필요 없음)
                for (Zone &zone : zones) {
                    if (!zone.name.startsWith(zoneId))
                        continue;
                    const ZoneState oldState = zone.state;
                    zone.temp = temp;
                    zone.humidity = humidity;
                    zone.gasPpm = gasPpm;
                    zone.smokePpm = smokePpm;
                    zone.flameVal = flameVal;
                    zone.state = zoneStateFromString(state);
                    zone.cause = cause;
                    zone.responseOk = responseOk;
                    zone.clearSensor = clearSensor;
                    zone.clearVision = clearVision;
                    zone.clearActuator = clearActuator;
                    zone.sensorOk = sensorOk;
                    zone.dhtOk = dhtOk;
                    zone.dangerSource = dangerSource;
                    zone.admin = admin;
                    zone.hasLiveSensorData = true;
                    if (oldState != zone.state) {
                        zone.stateEnteredAt = QDateTime::currentDateTime();
                        // 경고/위험 진입, 해제 등 상태가 바뀐 시점 = 서버가 이번 tick에 event_log를
                        // 남겼을 시점이라 바로 재조회한다 (이벤트로그 "실시간" 반영, 30초 안 기다림).
                        eventLogPage->requestRefresh();
                    }

                    zone.gasHistory.append(gasPpm);
                    zone.gasHistoryLabels.append(QDateTime::currentDateTime().toString("HH:mm:ss"));
                    constexpr int kMaxGasHistory = 30;
                    if (zone.gasHistory.size() > kMaxGasHistory) {
                        zone.gasHistory.removeFirst();
                        zone.gasHistoryLabels.removeFirst();
                    }

                    zone.flameHistory.append(flameVal);
                    if (zone.flameHistory.size() > kMaxGasHistory)
                        zone.flameHistory.removeFirst();

                    zone.smokeHistory.append(smokePpm);
                    if (zone.smokeHistory.size() > kMaxGasHistory)
                        zone.smokeHistory.removeFirst();

                    zone.smokeDetectHistory.append(smokePpm > 150);
                    constexpr int kMaxSmokeHistory = 8;
                    if (zone.smokeDetectHistory.size() > kMaxSmokeHistory)
                        zone.smokeDetectHistory.removeFirst();
                    // Warning으로 새로 바뀐 순간에만 상단 알림 생성 (계속 warning이면 내용만 갱신)
                    if (oldState != ZoneState::Warning && zone.state == ZoneState::Warning) {
                        showWarningAlert(zone.name, zoneId, cause, warnRemain);
                    } else if (zone.state == ZoneState::Warning) {
                        // 이미 알림이 떠 있으면 카운트다운만 서버 값으로 갱신 (Qt는 직접 시간을 재지 않는다)
                        if (WarningAlertDialog *dlg = activeWarningDialogs.value(zoneId))
                            dlg->setRemainingSeconds(warnRemain);
                    }
                    if (oldState == ZoneState::Warning && zone.state != ZoneState::Warning) {
                        // 경고를 벗어나면 상단 알림 자동 제거
                        if (WarningAlertDialog *dlg = activeWarningDialogs.value(zoneId))
                            dlg->dismiss();
                    }
                    // 경고/위험/해제 로그는 서버(server_main.cpp)가 db.insertEvent()로 이미 남기므로
                    // Qt는 여기서 더 이상 로컬로 addEntry()를 만들지 않는다 (안 그러면 한 사건이 두 줄로 뜸).
                    break;
                }
                if (!zones.isEmpty() && zones[currentZone].name.startsWith(zoneId))
                    refreshZoneUi();
                updateDangerIndicators();
            });

    stack = new QStackedWidget(centralArea);
    stack->addWidget(monitorPage);
    stack->addWidget(eventLogPage);
    stack->addWidget(graphPage);
    floorMapTabIndex = stack->addWidget(floorMapPage);
    stack->addWidget(helpPage);
    rootLayout->addWidget(stack);

    connect(floorMapPage, &FloorMapPage::configuredChanged, this, [this](bool) {
        refreshFloorMapTabBadge();
    });
    refreshFloorMapTabBadge();

    setCentralWidget(centralArea);

    switchTab(0);
    switchZone(0);

    monitorPage->connectCameras(kMediaMtxHost);

    connect(serverLink, &ServerLink::queryResult, this,
            [this](const QString &reqId, const QString &target, const QJsonArray &rows) {
                if (target == "event_log") {
                    eventLogPage->loadEntriesFromServer(rows);
                    // 같은 응답을 그래프에도 넘겨서 경고/위험 시점을 그래프 위 마커로 표시한다
                    // (이벤트로그가 이미 주기적으로 조회하고 있어서 별도 조회를 안 늘려도 된다).
                    graphPage->setEventMarkersFromServer(rows);
                } else if (target == "sensor_log") {
                    // 그래프 탭이 요청한 응답이면 그쪽으로, 아니면(초기 프리필) 기존대로 A구역 실시간
                    // 미니그래프 이력에 채운다. target이 같아서 reqId로 구분해야 한다.
                    if (!pendingGraphReqId.isEmpty() && reqId == pendingGraphReqId) {
                        pendingGraphReqId.clear();
                        graphPage->loadSensorLogFromServer(rows);
                        return;
                    }
                    for (Zone &zone : zones) {
                        if (!zone.name.startsWith("A"))
                            continue;
                        for (const QJsonValue &v : rows) {
                            const QJsonObject row = v.toObject();
                            zone.gasHistory.append(row.value("gasAvg").toDouble());
                            zone.gasHistoryLabels.append(
                                QDateTime::fromSecsSinceEpoch(qint64(row.value("t").toDouble())).toString("HH:mm:ss"));
                        }
                        break;
                    }
                    if (!zones.isEmpty() && zones[currentZone].name.startsWith("A"))
                        refreshZoneUi();
                }
            });
    connect(serverLink, &ServerLink::queryFailed, this,
            [this](const QString &reqId, const QString &reason) {
                if (!pendingGraphReqId.isEmpty() && reqId == pendingGraphReqId) {
                    pendingGraphReqId.clear();
                    graphPage->showQueryFailed(reason);
                }
            });

    connect(serverLink, &ServerLink::connectionStateChanged, this, [this](bool connected) {
        socketConnected = connected;
        if (connected)
            lastSensorMsgAt = QDateTime::currentDateTime(); // 방금 붙었으니 아직 끊긴 걸로 오판하면 안 됨
        refreshConnBadge();
        if (!connected)
            return;
        // 프로그램 시작 시 1회: 이벤트로그 최근 24시간 + 그래프용 센서 이력 직전 10분치.
        const qint64 now = QDateTime::currentSecsSinceEpoch();

        QJsonObject eventParams;
        eventParams["from"] = now - 24 * 3600;
        eventParams["to"] = now;
        eventParams["limit"] = 100;
        serverLink->sendQuery("event_log", eventParams);

        QJsonObject sensorParams;
        sensorParams["from"] = now - 600;
        sensorParams["to"] = now;
        sensorParams["zone"] = "A";
        serverLink->sendQuery("sensor_log", sensorParams);

        graphPage->requestCurrentPeriod(); // 그래프 탭도 현재 선택된 기간/날짜로 최초 조회

        // 평면도는 ROI와 달리 서버가 접속 즉시 push하지 않으므로 직접 query — 서버 재시작 후에도
        // FloorMapStore_Load()로 복원해둔 결과가 있으면 그걸 그대로 받아온다.
        serverLink->sendQuery("floor_map", QJsonObject());
    });

    // 이벤트로그 "조회" 버튼/30초 자동 갱신 — 응답은 위 queryResult 핸들러가 그대로 받아서
    // eventLogPage->loadEntriesFromServer()로 넘겨준다.
    connect(eventLogPage, &EventLogPage::eventLogRequested, this,
            [this](qint64 from, qint64 to) {
                QJsonObject params;
                params["from"] = from;
                params["to"] = to;
                params["limit"] = 500;
                serverLink->sendQuery("event_log", params);
            });

    // 이벤트로그 상세 패널 "재생" — clipPath를 그대로 실어 서버에 clip을 요청하고,
    // 반환된 reqId를 EventLogPage에 등록해서 나중에 그 응답인지 판별하게 한다(PR #63).
    connect(eventLogPage, &EventLogPage::clipPlayRequested, this,
            [this](const QString &path) {
                QJsonObject params;
                params["path"] = path;
                const QString reqId = serverLink->sendQuery("clip", params);
                eventLogPage->trackClipRequest(reqId);
            });
    connect(serverLink, &ServerLink::clipReceived, eventLogPage, &EventLogPage::onClipReceived);

    // sensor 메시지 흐름 감시(emergency-mode #19). 소켓은 붙어있어도 서버 내부(센서 스레드)가 멎으면
    // sensor가 안 오는데, connectionStateChanged만 보면 이 상태를 "연결됨"으로 잘못 표시하게 된다.
    sensorWatchdogTimer = new QTimer(this);
    connect(sensorWatchdogTimer, &QTimer::timeout, this, &MainWindow::refreshConnBadge);
    sensorWatchdogTimer->start(1000);

    serverLink->connectToServer(ServerConfig::kServerHost, ServerConfig::kServerPort);
}

// 상태색으로 배경을 살짝 물들인 알약 배지로 통일 — 텍스트 색만 바뀌던 예전보다 한눈에 들어온다.
static void applyConnBadgeStyle(QLabel *badge, const QString &color, const QString &bgColor)
{
    badge->setStyleSheet(QString(
        "color:%1; background-color:%2; border:1px solid %1; border-radius:12px; "
        "padding:6px 14px; font-size:14px; font-weight:bold; font-family:\"hanwhaGothic EL\";")
        .arg(color, bgColor));
}

void MainWindow::refreshConnBadge()
{
    if (!socketConnected) {
        connBadge->setText("● 서버 연결 끊김");
        applyConnBadgeStyle(connBadge, "#f87171", "#3a1f1f");
        connBadge->setToolTip("");
        return;
    }
    // 5초 이상 sensor 메시지가 안 오면 서버 내부가 멎은 것으로 판단 (sensor는 매초 고정 주기로 옴).
    const bool stale = lastSensorMsgAt.isValid() && lastSensorMsgAt.msecsTo(QDateTime::currentDateTime()) > 5000;
    if (stale) {
        connBadge->setText("● 서버 응답 없음 (5초+)");
        applyConnBadgeStyle(connBadge, "#fbbf24", "#3a3312");
        connBadge->setToolTip("TCP 연결은 살아있지만 서버로부터 센서 데이터가 5초 이상 오지 않고 있습니다.\n서버(server_main) 프로세스 상태를 확인해야 합니다.");
    } else {
        connBadge->setText("● 서버 실시간 연결 중");
        applyConnBadgeStyle(connBadge, "#34d399", "#14332a");
        connBadge->setToolTip("");
    }
}

QWidget *MainWindow::createDangerBanner()
{
    dangerBanner = new QPushButton(this);
    dangerBanner->setCursor(Qt::PointingHandCursor);
    dangerBanner->setStyleSheet(
        "QPushButton { background-color:#7f1d1d; color:white; font-size:22px; font-weight:bold; font-family:\"hanwhaGothic EL\"; "
        "border:none; padding:20px 16px; text-align:center; }"
        "QPushButton:hover { background-color:#991b1b; }");
    dangerBanner->setVisible(false);
    connect(dangerBanner, &QPushButton::clicked, this, [this]() {
        if (dangerBannerZoneIndex >= 0) {
            switchTab(0);
            switchZone(dangerBannerZoneIndex);
        }
    });
    return dangerBanner;
}

QWidget *MainWindow::createWarningAlertArea()
{
    warningAlertArea = new QWidget(centralArea);
    warningAlertArea->setFixedWidth(560);
    warningAlertArea->setStyleSheet("background:transparent;");
    warningAlertLayout = new QVBoxLayout(warningAlertArea);
    warningAlertLayout->setContentsMargins(0, 0, 0, 0);
    warningAlertLayout->setSpacing(6);
    auto *shadow = new QGraphicsDropShadowEffect(warningAlertArea);
    shadow->setBlurRadius(28);
    shadow->setOffset(0, 7);
    shadow->setColor(QColor(0, 0, 0, 180));
    warningAlertArea->setGraphicsEffect(shadow);
    warningAlertArea->setVisible(false);
    return warningAlertArea;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (warningAlertArea && warningAlertArea->isVisible())
        positionWarningAlert(false);
}

void MainWindow::positionWarningAlert(bool animate)
{
    if (!warningAlertArea || !centralArea)
        return;
    const int availableWidth = qMax(320, centralArea->width() - 32);
    warningAlertArea->setFixedWidth(qMin(560, availableWidth));
    warningAlertArea->adjustSize();
    const QPoint endPosition((centralArea->width() - warningAlertArea->width()) / 2, 10);
    warningAlertArea->raise();
    if (!animate) {
        warningAlertArea->move(endPosition);
        return;
    }
    warningAlertArea->move(endPosition.x(), -warningAlertArea->height() - 12);
    auto *animation = new QPropertyAnimation(warningAlertArea, "pos", warningAlertArea);
    animation->setDuration(320);
    animation->setStartValue(warningAlertArea->pos());
    animation->setEndValue(endPosition);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, animation, &QObject::deleteLater);
    animation->start();
}

QWidget *MainWindow::createTopBar()
{
    auto *bar = new QFrame(this);
    bar->setStyleSheet(QString("background-color:%1; border-bottom:1px solid %2;").arg(kBg, kCardBorder));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(16);

    auto *title = new QLabel("SafeVision", bar);
    title->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
    layout->addWidget(title);

    const QString zoneBtnStyle = QString(
        "QPushButton { color:%1; background:transparent; border:1px solid %2; border-radius:14px; padding:8px 20px; font-size:14px; }"
        "QPushButton:checked { background-color:%3; color:white; border:1px solid %3; }")
        .arg(kTextSecondary, kCardBorder, kAccent);

    for (int i = 0; i < zones.size(); ++i) {
        auto *btn = new QPushButton(zones[i].name, bar);
        btn->setCheckable(true);
        btn->setStyleSheet(zoneBtnStyle);
        connect(btn, &QPushButton::clicked, this, [this, i]() { switchZone(i); });
        layout->addWidget(btn);
        zoneButtons.append(btn);
    }

    layout->addStretch();

    topStatusLabel = new QLabel(bar);
    topStatusLabel->setStyleSheet(QString("border:1px solid %1; border-radius:12px; padding:6px 14px; font-size:14px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kCardBorder));
    layout->addWidget(topStatusLabel);

    connBadge = new QLabel(bar);
    layout->addWidget(connBadge);
    applyConnBadgeStyle(connBadge, kTextSecondary, "#1c1c2b");
    connBadge->setText("● 서버 연결 확인 중...");

    // 카메라 좌표 보정 — 자주 안 쓰는 설치 작업이라 자리는 오른쪽 끝에 두되, 버튼 자체는
    // 관리자가 찾기 쉽게 눈에 띄는 아웃라인 버튼으로(예전엔 투명 텍스트라 눈에 안 띔).
    auto *arucoBtn = new QPushButton("⚙ 카메라 좌표 보정", bar);
    arucoBtn->setCursor(Qt::PointingHandCursor);
    arucoBtn->setStyleSheet(QString(
        "QPushButton { color:%1; background-color:%2; border:1px solid %3; border-radius:14px; "
        "padding:8px 18px; font-size:14px; font-weight:bold; }"
        "QPushButton:hover { border:1px solid %4; color:%4; }")
        .arg(kTextPrimary, kCardBg, kCardBorder, kAccent));
    connect(arucoBtn, &QPushButton::clicked, this, [this]() {
        auto *dialog = new ArucoCalibrationDialog(serverLink, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->exec();
    });
    layout->addWidget(arucoBtn);

    // 로그인 계정은 지금 "admin" 하나로 고정돼 있다(LoginPage::kValidId) — 여러 계정을
    // 지원하게 되면 그때 로그인 성공 시그널에 실제 ID를 실어 여기로 넘기면 된다.
    auto *logoutBtn = new QPushButton("관리자 admin(ID) 로그아웃", bar);
    logoutBtn->setCursor(Qt::PointingHandCursor);
    logoutBtn->setStyleSheet(QString(
        "QPushButton { color:%1; background-color:%2; border:1px solid %3; border-radius:14px; "
        "padding:8px 18px; font-size:14px; font-weight:bold; }"
        "QPushButton:hover { border:1px solid #f87171; color:#f87171; }")
        .arg(kTextPrimary, kCardBg, kCardBorder));
    connect(logoutBtn, &QPushButton::clicked, this, [this]() {
        emit loggedOut();
        close();
    });
    layout->addWidget(logoutBtn);

    return bar;
}

QWidget *MainWindow::createSubTabBar()
{
    auto *bar = new QFrame(this);
    bar->setStyleSheet(QString("background-color:%1; border-bottom:1px solid %2;").arg(kBg, kCardBorder));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(16, 5, 16, 5);
    layout->setSpacing(4);

    const QString tabStyle = QString(
        "QPushButton { color:%1; background:transparent; border:none; border-radius:6px; padding:6px 12px; font-size:13px; }"
        "QPushButton:checked { background-color:%2; color:white; }")
        .arg(kTextSecondary, kAccentDark);

    for (int i = 0; i < kTabNames.size(); ++i) {
        auto *btn = new QPushButton(kTabNames[i], bar);
        btn->setCheckable(true);
        btn->setStyleSheet(tabStyle);
        connect(btn, &QPushButton::clicked, this, [this, i]() { switchTab(i); });
        layout->addWidget(btn);
        tabButtons.append(btn);
    }
    layout->addStretch();
    return bar;
}

void MainWindow::switchTab(int index)
{
    stack->setCurrentIndex(index);
    for (int i = 0; i < tabButtons.size(); ++i)
        tabButtons[i]->setChecked(i == index);
    updateDangerIndicators(); // 모니터링 탭 강조는 현재 탭에 따라 달라짐
    refreshFloorMapTabBadge(); // 평면도 탭 배지도 마찬가지 — 그 탭을 보고 있으면 숨김
}

void MainWindow::refreshFloorMapTabBadge()
{
    if (floorMapTabIndex < 0 || floorMapTabIndex >= tabButtons.size())
        return;
    const bool showBadge = !floorMapPage->isConfigured() && stack->currentIndex() != floorMapTabIndex;
    tabButtons[floorMapTabIndex]->setText(showBadge
        ? QString("❗ %1").arg(kTabNames[floorMapTabIndex])
        : kTabNames[floorMapTabIndex]);
}

void MainWindow::switchZone(int index)
{
    currentZone = index;
    for (int i = 0; i < zoneButtons.size(); ++i)
        zoneButtons[i]->setChecked(i == index);
    refreshZoneUi();
}

void MainWindow::setZoneState(int zoneIndex, ZoneState state)
{
    const ZoneState oldState = zones[zoneIndex].state;
    zones[zoneIndex].state = state;
    if (oldState != state)
        zones[zoneIndex].stateEnteredAt = QDateTime::currentDateTime();
    const QString zoneId = zones[zoneIndex].name.left(1);
    // DEMO 버튼으로도 sensorReceived와 동일하게 "새로 Warning 진입" 시 팝업 (테스트용).
    // 경고 단계 원인은 3가지(smoke_visual/fire_visual/flame_sensor) 중 데모에서는 smoke_visual로 고정.
    // 실제 warnRemain은 서버만 아니까, 데모에서는 로컬 타이머로 10초를 직접 세어 실제 흐름을 재현한다.
    if (oldState != ZoneState::Warning && state == ZoneState::Warning) {
        const QString &zoneName = zones[zoneIndex].name;
        showWarningAlert(zoneName, zoneId, "smoke_visual", 10);
        startDemoWarningCountdown(zoneIndex, zoneId, 10);
    }
    if (oldState == ZoneState::Warning && state != ZoneState::Warning) {
        stopDemoWarningCountdown();
        if (WarningAlertDialog *dlg = activeWarningDialogs.value(zoneId))
            dlg->dismiss();
    }
    // DEMO 버튼은 로컬 시뮬레이션이라 서버 DB에 안 남음 — 이벤트로그는 이제 서버 조회로만 채워지므로
    // 여기서 로컬 addEntry()를 만들지 않는다. (DEMO로 위험을 눌러도 로그 목록엔 안 뜨는 게 정상)
    if (zoneIndex == currentZone)
        refreshZoneUi();
    updateDangerIndicators();
}

void MainWindow::refreshZoneUi()
{
    const Zone &zone = zones[currentZone];
    monitorPage->updateZone(zone);
    eventLogPage->updateZone(zone);
    graphPage->updateZone(zone);

    const QString color = colorForState(zone.state);
    topStatusLabel->setText(QString("● %1 %2").arg(zone.name, textForState(zone.state)));
    topStatusLabel->setStyleSheet(QString("color:%1; border:1px solid %1; border-radius:12px; padding:6px 14px; font-size:14px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(color));
}

void MainWindow::showWarningAlert(const QString &zoneName, const QString &zoneId, const QString &cause, int warnRemain)
{
    // 경고 진입 로그는 서버가 남기므로 Qt는 상단 알림과 ACK 전송만 담당한다.
    // 단, "관리자가 확인 버튼을 눌렀다"는 이벤트는 서버가 아직 DB에 기록하지 않음(alarm_state.h의
    // onWarningAck()는 타이머 취소 플래그만 세움) — 서버에 로깅 추가되기 전까진 이 정보가 로그에 안 남는다.
    const QString causePhrase = causeText(cause);
    if (WarningAlertDialog *existing = activeWarningDialogs.value(zoneId)) {
        existing->setRemainingSeconds(warnRemain);
        return;
    }
    auto *dialog = new WarningAlertDialog(zoneName, causePhrase, warnRemain, warningAlertArea);
    warningAlertLayout->addWidget(dialog);
    warningAlertArea->setVisible(true);
    positionWarningAlert(true);
    activeWarningDialogs.insert(zoneId, dialog);
    connect(dialog, &WarningAlertDialog::acknowledged, this, [this, zoneId]() {
        serverLink->sendWarningAck(zoneId, "admin");
    });
    connect(dialog, &WarningAlertDialog::finished, this, [this, zoneId, dialog]() {
        if (activeWarningDialogs.value(zoneId) == dialog)
            activeWarningDialogs.remove(zoneId);
        // 확인 버튼으로 닫혔든, 상태 전환으로 자동으로 닫혔든 DEMO 카운트다운은 더 이상 필요 없다.
        stopDemoWarningCountdown();
        dialog->deleteLater();
        warningAlertArea->setVisible(!activeWarningDialogs.isEmpty());
    });
    dialog->show();
}

void MainWindow::startDemoWarningCountdown(int zoneIndex, const QString &zoneId, int seconds)
{
    stopDemoWarningCountdown();
    demoWarningZoneIndex = zoneIndex;
    demoWarningZoneId = zoneId;
    demoWarningRemain = seconds;
    if (!demoWarningTimer) {
        demoWarningTimer = new QTimer(this);
        connect(demoWarningTimer, &QTimer::timeout, this, [this]() {
            --demoWarningRemain;
            if (WarningAlertDialog *dlg = activeWarningDialogs.value(demoWarningZoneId))
                dlg->setRemainingSeconds(demoWarningRemain);
            if (demoWarningRemain <= 0) {
                demoWarningTimer->stop();
                // 서버 없는 DEMO에서도 "10초 무응답 -> 자동 위험 전환" 흐름을 그대로 재현.
                if (demoWarningZoneIndex >= 0 && demoWarningZoneIndex < zones.size()
                        && zones[demoWarningZoneIndex].state == ZoneState::Warning) {
                    setZoneState(demoWarningZoneIndex, ZoneState::Danger);
                }
            }
        });
    }
    demoWarningTimer->start(1000);
}

void MainWindow::stopDemoWarningCountdown()
{
    if (demoWarningTimer)
        demoWarningTimer->stop();
}

void MainWindow::updateDangerIndicators()
{
    int dangerZoneIndex = -1;
    for (int i = 0; i < zones.size(); ++i) {
        if (zones[i].state == ZoneState::Danger) {
            dangerZoneIndex = i;
            break;
        }
    }
    const bool anyDanger = dangerZoneIndex >= 0;

    // 1) 위험 배너: 클릭하면 해당 구역 모니터링으로 이동, 해제되면 사라짐.
    dangerBannerZoneIndex = dangerZoneIndex;
    if (anyDanger) {
        const Zone &dz = zones[dangerZoneIndex];
        const QString situation = causeText(dz.cause).isEmpty() ? "위험 상태 발생" : causeText(dz.cause);
        // 수동 발령은 발령자만 아는 근거(육안 확인, 냄새 등)가 있을 수 있어, 자동 감지와 구분해서
        // 발령자 이름을 같이 보여준다 — 다른 사람이 근거 없이 함부로 해제하면 안 되기 때문 (emergency-mode #17).
        const QString sourceText = dz.dangerSource == "manual"
            ? QString("수동 발령%1").arg(dz.admin.isEmpty() ? "" : QString(" (관리자: %1)").arg(dz.admin))
            : "자동 감지";
        dangerBanner->setText(QString("🚨 %1 %2 · %3 (클릭 시 모니터링으로 이동)").arg(dz.name, situation, sourceText));
        dangerBanner->setVisible(true);
    } else {
        dangerBanner->setVisible(false);
    }

    // 2) 화면 가장자리 글로우: safe로 복귀할 때까지 은은하게 숨쉬듯 유지.
    dangerGlow->setActive(anyDanger);

    // 3) 모니터링 탭 강조: 다른 탭을 보고 있을 때만 보조 신호로 표시.
    if (!tabButtons.isEmpty()) {
        const bool showDot = anyDanger && stack->currentIndex() != 0;
        tabButtons[0]->setText(showDot ? QString("🔴 %1").arg(kTabNames[0]) : kTabNames[0]);
    }
}

