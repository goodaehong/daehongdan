#include "MainWindow.h"
#include "../pages/MonitorPage.h"
#include "../pages/EventLogPage.h"
#include "../pages/GraphPage.h"
#include "../pages/ControlPage.h"
#include "../pages/HelpPage.h"
#include "../network/ServerLink.h"
#include "../widgets/WarningAlertDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QStackedWidget>
#include <QTimer>

namespace {
const QString kMediaMtxHost = "172.20.35.53"; // MediaMTX가 도는 라즈베리파이 주소 (카메라 IP 아님)
const QString kServerHost = "172.20.35.53";   // 감지/센서/제어 JSON 소켓도 같은 라즈베리파이
const quint16 kServerPort = 9999;             // TODO: 실제 서버 리슨 포트로 맞추기

const QStringList kTabNames = { "모니터링", "이벤트로그", "그래프", "수동제어", "도움말" };

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

    rootLayout->addWidget(createDangerBanner());
    rootLayout->addWidget(createTopBar());
    rootLayout->addWidget(createSubTabBar());

    dangerPulseTimer = new QTimer(this);
    connect(dangerPulseTimer, &QTimer::timeout, this, [this]() {
        dangerPulseOn = !dangerPulseOn;
        const QString borderColor = dangerPulseOn ? "#f87171" : "#7f1d1d";
        centralArea->setStyleSheet(QString("background-color:%1; border:4px solid %2;").arg(kBg, borderColor));
    });

    monitorPage = new MonitorPage(centralArea);
    eventLogPage = new EventLogPage(centralArea);
    graphPage = new GraphPage(centralArea);
    controlPage = new ControlPage(centralArea);
    helpPage = new HelpPage(centralArea);

    connect(monitorPage, &MonitorPage::demoStateRequested, this, [this](ZoneState state) {
        setZoneState(currentZone, state);
    });

    serverLink = new ServerLink(this);

    connect(controlPage, &ControlPage::controlRequested, this,
            [this](const QString &target, const QString &action, const QString &title) {
                const QString zoneName = zones[currentZone].name;
                const QString zoneId = zoneName.left(1); // "A구역" -> "A"

                // ★ 여기가 진짜 실행되는지 디버그 로그로 확인해야 합니다!
                qDebug() << "[MainWindow] 버튼 클릭 감지! 제어 송신 시도:" << target << action;

                const QString cmdId = serverLink->sendControl(zoneId, target, action, "admin");
                pendingControlTitles.insert(cmdId, title);

                // 서버(actuator_status) 응답을 기다리지 않고 낙관적으로 모니터링 종합상태에도 반영.
                // 실제 서버가 없거나 아직 actuator_status를 안 보내는 동안에도 확인 가능하도록.
                if (target == "fan") {
                    if (action == "off") currentFan = 0;
                    else if (action == "low") currentFan = 1;
                    else if (action == "mid") currentFan = 2;
                    else if (action == "high") currentFan = 3;
                } else if (target == "valve") {
                    if (action == "close") currentValve = 0;
                    else if (action == "open") currentValve = 1;
                } else if (target == "siren") {
                    if (action == "off") currentSiren = 0;
                    else if (action == "on") currentSiren = 1;
                }
                monitorPage->setActuatorStatus(currentFan, currentValve, currentSiren);
            });

    connect(serverLink, &ServerLink::controlResult, this,
            [this](const QString &cmdId, const QString &zone, const QString &, const QString &result, const QString &reason) {
                const QString title = pendingControlTitles.take(cmdId);
                if (title.isEmpty())
                    return;
                if (result == "ok")
                    eventLogPage->addEntry(zone + "구역", "관리자 수동 제어", title + " 완료", "admin", "정보", "-", "-");
                else
                    eventLogPage->addEntry(zone + "구역", "관리자 수동 제어", title + " 실패: " + reason, "admin", "경고", "-", "-");
            });
    connect(serverLink, &ServerLink::controlTimedOut, this,
            [this](const QString &cmdId, const QString &zone, const QString &) {
                const QString title = pendingControlTitles.take(cmdId);
                if (title.isEmpty())
                    return;
                eventLogPage->addEntry(zone + "구역", "관리자 수동 제어", title + " 응답 없음 (서버 연결 확인 필요)", "admin", "경고", "-", "-");
            });

    connect(serverLink, &ServerLink::actuatorStatusReceived, this,
            [this](int fan, int valve, int siren) {
                currentFan = fan;
                currentValve = valve;
                currentSiren = siren;
                controlPage->setFanLevel(fan);
                controlPage->setValveState(valve);
                controlPage->setSirenState(siren);
                monitorPage->setActuatorStatus(fan, valve, siren);
            });

    connect(serverLink, &ServerLink::detectionReceived, this,
            [this](int channel, int, int srcW, int srcH, bool, const QVector<DetectionBox> &boxes) {
                monitorPage->updateDetection(channel, srcW, srcH, boxes);
            });

    connect(serverLink, &ServerLink::sensorReceived, this,
            [this](const QString &zoneId, qint64, double temp, double humidity,
                   double gasPpm, double smokePpm, const QString &state) {
                for (Zone &zone : zones) {
                    if (!zone.name.startsWith(zoneId))
                        continue;
                    const ZoneState oldState = zone.state;
                    zone.temp = temp;
                    zone.humidity = humidity;
                    zone.gasPpm = gasPpm;
                    zone.smokePpm = smokePpm;
                    zone.state = zoneStateFromString(state);
                    zone.hasLiveSensorData = true;
                    // Warning으로 "새로" 바뀐 순간에만 팝업 (계속 warning이면 매번 안 뜸)
                    if (oldState != ZoneState::Warning && zone.state == ZoneState::Warning)
                        showWarningAlert(zone.name, zoneId);
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
    stack->addWidget(controlPage);
    stack->addWidget(helpPage);
    rootLayout->addWidget(stack);

    setCentralWidget(centralArea);

    switchTab(0);
    switchZone(0);

    monitorPage->connectCameras(kMediaMtxHost);
    serverLink->connectToServer(kServerHost, kServerPort);

    eventLogPage->addEntry("A구역", "가스 농도 상승", "경고 알림 전송", "시스템(자동)", "경고", "MQ-9 단독", "2분 10초");
    eventLogPage->addEntry("A구역", "연기 감지 (경고)", "경고 표시 (사이렌 X)", "시스템(자동)", "경고", "영상+MQ-2", "1분 40초");
    eventLogPage->addEntry("A구역", "정상 복귀", "모니터링 유지", "시스템(자동)", "안전", "-", "-");
}

QWidget *MainWindow::createDangerBanner()
{
    dangerBanner = new QPushButton(this);
    dangerBanner->setCursor(Qt::PointingHandCursor);
    dangerBanner->setStyleSheet(
        "QPushButton { background-color:#7f1d1d; color:white; font-size:14px; font-weight:bold; "
        "border:none; padding:10px 16px; text-align:left; }"
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
    layout->setContentsMargins(16, 10, 16, 10);
    layout->setSpacing(12);

    auto *title = new QLabel("통합 관제 플랫폼", bar);
    title->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold;").arg(kTextPrimary));
    layout->addWidget(title);

    const QString zoneBtnStyle = QString(
        "QPushButton { color:%1; background:transparent; border:1px solid %2; border-radius:12px; padding:4px 14px; }"
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
    topStatusLabel->setStyleSheet(QString("border:1px solid %1; border-radius:10px; padding:3px 10px; font-weight:bold;").arg(kCardBorder));
    layout->addWidget(topStatusLabel);

    auto *connBadge = new QLabel("<span style='color:#34d399;'>●</span> 실시간 연결 중", bar);
    connBadge->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    layout->addWidget(connBadge);

    auto *logoutBtn = new QPushButton("관리자모드 로그아웃", bar);
    logoutBtn->setFlat(true);
    logoutBtn->setStyleSheet(QString("color:%1; background:transparent; border:none;").arg(kTextSecondary));
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
    layout->setContentsMargins(16, 6, 16, 6);
    layout->setSpacing(4);

    const QString tabStyle = QString(
        "QPushButton { color:%1; background:transparent; border:none; border-radius:8px; padding:6px 14px; }"
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
    // DEMO 버튼으로도 sensorReceived와 동일하게 "새로 Warning 진입" 시 팝업 (테스트용).
    if (oldState != ZoneState::Warning && state == ZoneState::Warning) {
        const QString &zoneName = zones[zoneIndex].name;
        showWarningAlert(zoneName, zoneName.left(1));
    }
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
    controlPage->setZoneName(zone.name);

    const QString color = colorForState(zone.state);
    topStatusLabel->setText(QString("● %1 %2").arg(zone.name, textForState(zone.state)));
    topStatusLabel->setStyleSheet(QString("color:%1; border:1px solid %1; border-radius:10px; padding:3px 10px; font-weight:bold;").arg(color));
}

void MainWindow::showWarningAlert(const QString &zoneName, const QString &zoneId)
{
    // TODO: 서버가 sensor 메시지에 warnRemain(카운트다운 초)·cause(원인)를 추가하면
    // 아래 고정값(10초, 원인 없음) 대신 그 값을 그대로 넘기면 된다.
    constexpr int kFallbackCountdownSec = 10;
    auto *dialog = new WarningAlertDialog(zoneName, QString(), kFallbackCountdownSec, this);
    connect(dialog, &WarningAlertDialog::acknowledged, this, [this, zoneId]() {
        serverLink->sendWarningAck(zoneId, "ack", "admin");
    });
    connect(dialog, &WarningAlertDialog::timedOut, this, [this, zoneId]() {
        serverLink->sendWarningAck(zoneId, "timeout", "admin");
    });
    connect(dialog, &QDialog::finished, dialog, &QObject::deleteLater);
    dialog->show();
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
        // TODO: 서버가 sensor 메시지에 cause 필드를 추가하면 causeText(cause)로 원인문구를 붙인다.
        dangerBanner->setText(QString("🚨 %1 위험 상태 발생! (클릭 시 모니터링으로 이동)").arg(zones[dangerZoneIndex].name));
        dangerBanner->setVisible(true);
    } else {
        dangerBanner->setVisible(false);
    }

    // 2) 화면 테두리 펄스: safe로 복귀할 때까지 깜빡임 유지.
    if (anyDanger) {
        if (!dangerPulseTimer->isActive())
            dangerPulseTimer->start(600);
    } else {
        dangerPulseTimer->stop();
        centralArea->setStyleSheet(QString("background-color:%1;").arg(kBg));
    }

    // 3) 모니터링 탭 강조: 다른 탭을 보고 있을 때만 보조 신호로 표시.
    if (!tabButtons.isEmpty()) {
        const bool showDot = anyDanger && stack->currentIndex() != 0;
        tabButtons[0]->setText(showDot ? QString("🔴 %1").arg(kTabNames[0]) : kTabNames[0]);
    }
}
