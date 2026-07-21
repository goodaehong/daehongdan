#include "MainWindow.h"
#include "../pages/MonitorPage.h"
#include "../pages/EventLogPage.h"
#include "../pages/GraphPage.h"
#include "../pages/ControlPage.h"
#include "../pages/HelpPage.h"
#include "../network/TlsClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QStackedWidget>
#include <QDebug>

namespace {
const QString kCameraHost = "172.20.35.13";
const QString kCameraUser = "admin";
const QString kCameraPass = "5hanwha!";

// [새로 추가된 라즈베리파이 서버 IP 상수]
const QString kTlsServerHost = "172.29.174.236"; // ★ 확인하신 라즈베리파이 IP로 꼭 변경하세요!
const quint16 kTlsServerPort = 8443;
// [새로 추가된 라즈베리파이 서버 IP 상수]

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

    // ==========================================================
    // [새로 추가된 TLS 통신 파트]
    // ==========================================================
    
    // 1. TlsClient 객체 생성 (this를 부모로 두어 메모리 자동 해제)
    TlsClient *tlsClient = new TlsClient(this);

    // 2. 서버 연결 성공 시그널 처리
    connect(tlsClient, &TlsClient::connected, this, [=]() {
        qDebug() << "[TLS] 라즈베리파이 관제 서버와 암호화 통신이 연결되었습니다!";
        
        // 연결 성공 시점에 상단 바의 연결 상태 UI를 업데이트하거나 
        // 라즈베리파이에 초기 설정 데이터를 요구할 수 있습니다.
        tlsClient->sendData("{\"command\": \"GET_INITIAL_STATE\"}");
    });

    // 3. 서버로부터 데이터 수신 시그널 처리 (디버깅용 원본 로그)
    connect(tlsClient, &TlsClient::dataReceived, this, [=](const QByteArray &data) {
        qDebug() << "[TLS] 서버 수신 데이터:" << data;
    });

    // 3-1. 센서 데이터 수신 처리 (온도, 습도, 가스/연기 상태 업데이트)
    connect(tlsClient, &TlsClient::sensorReceived, this, [=](const QString &zoneName, qint64 ts, double temp, double humidity, double gasPpm, double smokePpm, const QString &state) {
        qDebug() << "[TLS] 센서 데이터 수신 - 구역:" << zoneName 
                 << "온도:" << temp << "습도:" << humidity 
                 << "가스:" << gasPpm << "연기:" << smokePpm << "상태:" << state;
        
        // 구역 이름 매칭 및 데이터 업데이트
        for (int i = 0; i < zones.size(); ++i) {
            if (zones[i].name == zoneName) {
                zones[i].temp = temp;
                zones[i].humidity = humidity;
                
                // 상태 변환 (Safe, Warning, Danger)
                ZoneState stateEnum = ZoneState::Safe;
                if (state == "Warning") stateEnum = ZoneState::Warning;
                else if (state == "Danger") stateEnum = ZoneState::Danger;
                
                setZoneState(i, stateEnum);
                break;
            }
        }
    });

    // 3-2. 영상 객체 감지 데이터 수신 처리
    connect(tlsClient, &TlsClient::detectionReceived, this, [=](int channel, int frameId, int srcW, int srcH, bool alarm, const QJsonArray &boxes) {
        qDebug() << "[TLS] 영상 객체 감지 수신 - 채널:" << channel 
                 << "프레임 ID:" << frameId << "알람여부:" << alarm << "감지객체수:" << boxes.size();
        
        if (alarm) {
            // 알람이 감지된 경우 이벤트 로그에 추가
            eventLogPage->addEntry(
                QString("%1구역 (카메라 %2)").arg(zones[currentZone].name).arg(channel),
                "AI 영상 분석 화재/위험 감지",
                "경고 화면 표시 및 관리자 알림",
                "시스템(자동)",
                "위험",
                "카메라 영상",
                "-"
            );
        }
    });

    // 3-3. 액추에이터(환기팬, 밸브, 사이렌) 상태 수신 처리
    connect(tlsClient, &TlsClient::actuatorStatusReceived, this, [=](int fan, int valve, int siren) {
        qDebug() << "[TLS] 액추에이터 상태 수신 - 환기팬:" << (fan ? "ON" : "OFF") 
                 << "솔레노이드 밸브:" << (valve ? "OPEN" : "CLOSE") 
                 << "경고 사이렌:" << (siren ? "ON" : "OFF");
                 
        // 이벤트 로그에 액추에이터 상태 변경 내용 기록
        eventLogPage->addEntry(
            zones[currentZone].name,
            "액추에이터 상태 변경 수신",
            QString("환기팬:%1 | 밸브:%2 | 사이렌:%3").arg(fan ? "ON" : "OFF").arg(valve ? "OPEN" : "CLOSE").arg(siren ? "ON" : "OFF"),
            "시스템(자동)",
            "정보",
            "-",
            "-"
        );
    });

    // 3-4. 전광판(LED Matrix) 상태 수신 처리
    connect(tlsClient, &TlsClient::ledMatrixStatusReceived, this, [=](int status) {
        qDebug() << "[TLS] 전광판 상태 수신 - 상태코드:" << status;
        
        // 이벤트 로그에 전광판 상태 변경 내용 기록
        eventLogPage->addEntry(
            zones[currentZone].name,
            "전광판 상태 수신",
            QString("전광판 표시 상태: %1 (코드:%2)").arg(status == 1 ? "정상 안내" : (status == 2 ? "화재 경고" : "대피 안내")).arg(status),
            "시스템(자동)",
            "정보",
            "-",
            "-"
        );
    });
    
    // 4. 수동 제어 페이지(ControlPage)에서 버튼을 누르면 서버로 명령 전송
    // (ControlPage에 sendCommand 등의 시그널이 있다고 가정)
    /* 
    connect(controlPage, &ControlPage::sendCommand, this, [=](const QString &cmd) {
        tlsClient->sendData(cmd.toUtf8());
    });
    */

    // 5. 프로그램 구동과 동시에 서버 접속 시도
    tlsClient->connectToServer(kTlsServerHost, kTlsServerPort);
    // ==========================================================
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
