#include "MainWindow.h"
#include "../pages/MonitorPage.h"
#include "../pages/EventLogPage.h"
#include "../pages/GraphPage.h"
#include "../pages/HelpPage.h"
#include "../network/ServerLink.h"
#include "../widgets/WarningAlertDialog.h"
#include "../widgets/DangerGlowOverlay.h"

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

namespace {
const QString kMediaMtxHost = "172.20.35.230"; // MediaMTX가 도는 라즈베리파이 주소 (카메라 IP 아님)
const QString kServerHost = "172.20.35.230";   // 감지/센서/제어 JSON 소켓도 같은 라즈베리파이
const quint16 kServerPort = 9999;             // TODO: 실제 서버 리슨 포트로 맞추기

const QStringList kTabNames = { "모니터링", "이벤트로그", "그래프", "도움말" };

const QString kBg = "#0a0a12";
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";
const QString kAccentDark = "#6a5cd6";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("공장 가스·화재 조기감지 및 자동대응 시스템");
    resize(1400, 860);

    zones.append({ "A구역", ZoneState::Warning, 24.3, 42.0 });
    zones.append({ "B구역", ZoneState::Safe, 23.1, 38.0 });

    centralArea = new QWidget(this);
    centralArea->setStyleSheet(QString("background-color:%1;").arg(kBg));
    auto *rootLayout = new QVBoxLayout(centralArea);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(createEvacuationBanner());
    rootLayout->addWidget(createDangerBanner());
    rootLayout->addWidget(createTopBar());
    rootLayout->addWidget(createSubTabBar());

    dangerGlow = new DangerGlowOverlay(this);

    monitorPage = new MonitorPage(centralArea);
    eventLogPage = new EventLogPage(centralArea);
    graphPage = new GraphPage(centralArea);
    helpPage = new HelpPage(centralArea);

    connect(monitorPage, &MonitorPage::demoStateRequested, this, [this](ZoneState state) {
        setZoneState(currentZone, state);
    });

    serverLink = new ServerLink(this);

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

    // 대피 모드는 별도 메시지 타입(evacuation_trigger/clear)이라 sendControl()과 분리해서 보낸다.
    // zone은 "발생 구역 표시용"일 뿐 실제 적용은 서버가 전 구역에 한다.
    connect(monitorPage, &MonitorPage::evacuationActionRequested, this,
            [this](bool activate) {
                const QString zoneId = zones[currentZone].name.left(1);
                const QString label = activate ? "대피 모드 발동" : "대피 모드 해제";
                if (activate)
                    serverLink->sendEvacuationTrigger(zoneId, "admin");
                else
                    serverLink->sendEvacuationClear(zoneId, "admin");
                monitorPage->showControlStatus(QString("처리 중... (%1)").arg(label), "#8d87a0");
            });

    connect(serverLink, &ServerLink::evacuationResult, this,
            [this](const QString &, const QString &, const QString &mode,
                   const QString &result, const QString &reason) {
                const QString label = mode == "trigger" ? "대피 모드 발동" : "대피 모드 해제";
                if (result == "ok") {
                    monitorPage->showControlStatus(QString("완료: %1").arg(label), "#34d399");
                    // evacuationActive 자체는 여기서 낙관적으로 바꾸지 않는다 -> sensor 메시지의
                    // evacuation 필드가 1초 이내로 실제 상태를 확정해서 알려준다.
                } else {
                    const QString reasonText = reason.isEmpty() ? "알 수 없는 오류" : reason;
                    monitorPage->showControlStatus(QString("실패: %1 (%2)").arg(label, reasonText), "#f87171");
                }
            });
    connect(serverLink, &ServerLink::evacuationTimedOut, this,
            [this](const QString &, const QString &, const QString &) {
                monitorPage->showControlStatus("응답 없음 — 서버 연결 확인 필요", "#f87171");
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
            });
    connect(serverLink, &ServerLink::controlTimedOut, this,
            [this](const QString &cmdId, const QString &, const QString &target) {
                pendingControlTitles.remove(cmdId);
                monitorPage->showControlStatus("응답 없음 — 서버 연결 확인 필요", "#f87171");
                monitorPage->setActuatorRowStatus(target, "응답 없음", "#f87171");
            });

    connect(serverLink, &ServerLink::actuatorStatusReceived, this,
            [this](int fan, int valve, int siren, const QString &link,
                   const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc) {
                currentFan = fan;
                currentValve = valve;
                currentSiren = siren;
                monitorPage->setActuatorStatus(fan, valve, siren, link, fanSrc, valveSrc, sirenSrc);
            });

    connect(serverLink, &ServerLink::detectionReceived, this,
            [this](int channel, int, int srcW, int srcH, bool alarm, const QVector<DetectionBox> &boxes) {
                monitorPage->updateDetection(channel, srcW, srcH, boxes);
                monitorPage->setChannelAlarm(channel, alarm);
            });

    connect(serverLink, &ServerLink::sensorReceived, this,
            [this](const QString &zoneId, qint64, double temp, double humidity,
                   double gasPpm, double smokePpm, double flameVal, const QString &state,
                   const QString &cause, int warnRemain, bool evacuationActive) {
                if (this->evacuationActive != evacuationActive) {
                    this->evacuationActive = evacuationActive;
                    updateEvacuationBanner();
                    monitorPage->setEvacuationActive(evacuationActive);
                }
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
                    zone.hasLiveSensorData = true;
                    if (oldState != zone.state)
                        zone.stateEnteredAt = QDateTime::currentDateTime();

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

                    zone.smokeDetectHistory.append(smokePpm > 150);
                    constexpr int kMaxSmokeHistory = 8;
                    if (zone.smokeDetectHistory.size() > kMaxSmokeHistory)
                        zone.smokeDetectHistory.removeFirst();
                    // Warning으로 "새로" 바뀐 순간에만 팝업 (계속 warning이면 매번 안 뜸)
                    if (oldState != ZoneState::Warning && zone.state == ZoneState::Warning) {
                        showWarningAlert(zone.name, zoneId, cause, warnRemain);
                    } else if (zone.state == ZoneState::Warning) {
                        // 이미 팝업이 떠 있으면 카운트다운만 서버 값으로 갱신 (Qt는 직접 시간을 재지 않는다)
                        if (WarningAlertDialog *dlg = activeWarningDialogs.value(zoneId))
                            dlg->setRemainingSeconds(warnRemain);
                    }
                    if (oldState == ZoneState::Warning && zone.state != ZoneState::Warning) {
                        // 경고를 벗어남(확인 없이도 안전 복귀 또는 서버가 위험으로 자동 전환) -> 팝업 자동 닫기
                        if (WarningAlertDialog *dlg = activeWarningDialogs.value(zoneId))
                            dlg->close();
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
    stack->addWidget(helpPage);
    rootLayout->addWidget(stack);

    setCentralWidget(centralArea);

    switchTab(0);
    switchZone(0);

    monitorPage->connectCameras(kMediaMtxHost);

    connect(serverLink, &ServerLink::queryResult, this,
            [this](const QString &, const QString &target, const QJsonArray &rows) {
                if (target == "event_log") {
                    eventLogPage->loadEntriesFromServer(rows);
                } else if (target == "sensor_log") {
                    // 초기 프리필은 A구역 하나만 요청하므로 그대로 매칭한다.
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

    connect(serverLink, &ServerLink::connectionStateChanged, this, [this](bool connected) {
        connBadge->setText(connected
            ? "<span style='color:#34d399;'>●</span> 실시간 연결 중"
            : "<span style='color:#f87171;'>●</span> 서버 연결 끊김");
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
    });

    serverLink->connectToServer(kServerHost, kServerPort);
}

QWidget *MainWindow::createEvacuationBanner()
{
    // 대피 모드는 zone과 무관한 전 구역 상태라, 위험 배너보다 위쪽에 두어 어느 탭/구역을 보고 있든
    // 항상 최우선으로 보이게 한다. 클릭하면 모니터링 탭으로 이동(해제 버튼이 거기 있으므로).
    evacuationBanner = new QPushButton("🚨 대피 모드 발동 중 — 클릭 시 모니터링으로 이동", this);
    evacuationBanner->setCursor(Qt::PointingHandCursor);
    evacuationBanner->setStyleSheet(
        "QPushButton { background-color:#ef4444; color:white; font-size:22px; font-weight:bold; "
        "border:none; padding:20px 16px; text-align:center; }"
        "QPushButton:hover { background-color:#dc2626; }");
    evacuationBanner->setVisible(false);
    connect(evacuationBanner, &QPushButton::clicked, this, [this]() {
        switchTab(0);
    });
    return evacuationBanner;
}

QWidget *MainWindow::createDangerBanner()
{
    dangerBanner = new QPushButton(this);
    dangerBanner->setCursor(Qt::PointingHandCursor);
    dangerBanner->setStyleSheet(
        "QPushButton { background-color:#7f1d1d; color:white; font-size:22px; font-weight:bold; "
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

QWidget *MainWindow::createTopBar()
{
    auto *bar = new QFrame(this);
    bar->setStyleSheet(QString("background-color:%1; border-bottom:1px solid %2;").arg(kBg, kCardBorder));
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(16);

    auto *title = new QLabel("통합 관제 플랫폼", bar);
    title->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold;").arg(kTextPrimary));
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
    topStatusLabel->setStyleSheet(QString("border:1px solid %1; border-radius:12px; padding:6px 14px; font-size:14px; font-weight:bold;").arg(kCardBorder));
    layout->addWidget(topStatusLabel);

    connBadge = new QLabel("<span style='color:#6b7280;'>●</span> 서버 연결 확인 중...", bar);
    connBadge->setStyleSheet(QString("color:%1; font-size:14px;").arg(kTextSecondary));
    layout->addWidget(connBadge);

    auto *logoutBtn = new QPushButton("관리자모드 로그아웃", bar);
    logoutBtn->setFlat(true);
    logoutBtn->setStyleSheet(QString("color:%1; background:transparent; border:none; font-size:14px;").arg(kTextSecondary));
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
            dlg->close();
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
    topStatusLabel->setStyleSheet(QString("color:%1; border:1px solid %1; border-radius:12px; padding:6px 14px; font-size:14px; font-weight:bold;").arg(color));
}

void MainWindow::showWarningAlert(const QString &zoneName, const QString &zoneId, const QString &cause, int warnRemain)
{
    // 경고 진입 로그는 서버(server_main.cpp)가 이미 db.insertEvent()로 남기므로 Qt는 팝업만 띄운다.
    // 단, "관리자가 확인 버튼을 눌렀다"는 이벤트는 서버가 아직 DB에 기록하지 않음(alarm_state.h의
    // onWarningAck()는 타이머 취소 플래그만 세움) — 서버에 로깅 추가되기 전까진 이 정보가 로그에 안 남는다.
    const QString causePhrase = causeText(cause);
    auto *dialog = new WarningAlertDialog(zoneName, causePhrase, warnRemain, this);
    activeWarningDialogs.insert(zoneId, dialog);
    connect(dialog, &WarningAlertDialog::acknowledged, this, [this, zoneId]() {
        serverLink->sendWarningAck(zoneId, "admin");
    });
    connect(dialog, &QDialog::finished, this, [this, zoneId, dialog]() {
        if (activeWarningDialogs.value(zoneId) == dialog)
            activeWarningDialogs.remove(zoneId);
        // 확인 버튼으로 닫혔든, 상태 전환으로 자동으로 닫혔든 DEMO 카운트다운은 더 이상 필요 없다.
        stopDemoWarningCountdown();
        dialog->deleteLater();
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
        const QString &cause = zones[dangerZoneIndex].cause;
        const QString situation = causeText(cause).isEmpty() ? "위험 상태 발생" : causeText(cause);
        dangerBanner->setText(QString("🚨 %1 %2! (클릭 시 모니터링으로 이동)").arg(zones[dangerZoneIndex].name, situation));
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

void MainWindow::updateEvacuationBanner()
{
    if (evacuationBanner)
        evacuationBanner->setVisible(evacuationActive);
}
