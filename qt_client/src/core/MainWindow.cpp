#include "MainWindow.h"
#include "../pages/MonitorPage.h"
#include "../pages/EventLogPage.h"
#include "../pages/GraphPage.h"
#include "../pages/ControlPage.h"
#include "../pages/HelpPage.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QStackedWidget>

namespace {
const QString kCameraHost = "172.20.35.13";
const QString kCameraUser = "admin";
const QString kCameraPass = "5hanwha!";

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

    auto *central = new QWidget(this);
    central->setStyleSheet(QString("background-color:%1;").arg(kBg));
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    rootLayout->addWidget(createTopBar());
    rootLayout->addWidget(createSubTabBar());

    monitorPage = new MonitorPage(central);
    eventLogPage = new EventLogPage(central);
    graphPage = new GraphPage(central);
    controlPage = new ControlPage(central);
    helpPage = new HelpPage(central);

    connect(monitorPage, &MonitorPage::demoStateRequested, this, [this](ZoneState state) {
        setZoneState(currentZone, state);
    });
    connect(controlPage, &ControlPage::actionLogged, this,
            [this](const QString &detection, const QString &response, const QString &admin,
                   const QString &severity, const QString &sensorCombo, const QString &duration) {
                eventLogPage->addEntry(zones[currentZone].name, detection, response, admin, severity, sensorCombo, duration);
            });

    stack = new QStackedWidget(central);
    stack->addWidget(monitorPage);
    stack->addWidget(eventLogPage);
    stack->addWidget(graphPage);
    stack->addWidget(controlPage);
    stack->addWidget(helpPage);
    rootLayout->addWidget(stack);

    setCentralWidget(central);

    switchTab(0);
    switchZone(0);

    monitorPage->connectCamera(kCameraHost, kCameraUser, kCameraPass, 0);

    eventLogPage->addEntry("A구역", "가스 농도 상승", "경고 알림 전송", "시스템(자동)", "경고", "MQ-9 단독", "2분 10초");
    eventLogPage->addEntry("A구역", "연기 감지 (경고)", "경고 표시 (사이렌 X)", "시스템(자동)", "경고", "영상+MQ-2", "1분 40초");
    eventLogPage->addEntry("A구역", "정상 복귀", "모니터링 유지", "시스템(자동)", "안전", "-", "-");
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
    zones[zoneIndex].state = state;
    if (zoneIndex == currentZone)
        refreshZoneUi();
}

void MainWindow::refreshZoneUi()
{
    const Zone &zone = zones[currentZone];
    monitorPage->updateZone(zone);
    eventLogPage->updateZone(zone);
    graphPage->updateZone(zone);
    controlPage->setZoneName(zone.name);
}
