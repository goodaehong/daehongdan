#include "mainwindow.h"
#include "gasgraphwidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QStackedWidget>
#include <QVideoWidget>
#include <QUrl>
#include <QDateTime>
#include <QGraphicsDropShadowEffect>
#include <QDialog>
#include <QColor>

namespace {
const QString kCameraHost = "172.20.35.13";
const QString kCameraUser = "admin";
const QString kCameraPass = "5hanwha!";

const QStringList kTabNames = { "모니터링", "이벤트로그", "그래프", "수동제어", "도움말" };
const QStringList kZoneFilterNames = { "전체", "A구역", "B구역" };

const QString kBg = "#0a0a12";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";
const QString kAccentDark = "#6a5cd6";

QString colorForState(ZoneState state)
{
    switch (state) {
    case ZoneState::Safe: return "#34d399";
    case ZoneState::Warning: return "#fbbf24";
    case ZoneState::Danger: return "#f87171";
    }
    return "#34d399";
}

QString textForState(ZoneState state)
{
    switch (state) {
    case ZoneState::Safe: return "안전";
    case ZoneState::Warning: return "경고";
    case ZoneState::Danger: return "위험";
    }
    return "안전";
}
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

    stack = new QStackedWidget(central);
    stack->addWidget(createMonitoringPage());
    stack->addWidget(createEventLogPage());
    stack->addWidget(createGraphPage());
    stack->addWidget(createManualControlPage());
    stack->addWidget(createHelpPage());
    rootLayout->addWidget(stack);

    setCentralWidget(central);

    switchTab(0);
    switchZone(0);

    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    audioOutput->setMuted(true);
    player->setAudioOutput(audioOutput);
    player->setVideoOutput(zoneVideoWidget);
    connect(player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &errorString) {
                cameraStatusValueLabel->setText("오류");
                cameraStatusValueLabel->setStyleSheet(QString("color:%1; font-weight:bold;").arg(colorForState(ZoneState::Danger)));
                Q_UNUSED(errorString);
            });
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::LoadedMedia) {
                    cameraStatusValueLabel->setText("정상");
                    cameraStatusValueLabel->setStyleSheet(QString("color:%1; font-weight:bold;").arg(colorForState(ZoneState::Safe)));
                }
            });

    const QString url = QString("rtsp://%1:%2@%3:554/0/profile2/media.smp")
                             .arg(kCameraUser, kCameraPass, kCameraHost);
    player->setSource(QUrl(url));
    player->play();

    addEventLog("A구역", "가스 농도 상승", "경고 알림 전송", "시스템(자동)", "경고", "MQ-9 단독", "2분 10초");
    addEventLog("A구역", "연기 감지 (경고)", "경고 표시 (사이렌 X)", "시스템(자동)", "경고", "영상+MQ-2", "1분 40초");
    addEventLog("A구역", "정상 복귀", "모니터링 유지", "시스템(자동)", "안전", "-", "-");
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
    const QString color = colorForState(zone.state);

    heroTitleLabel->setText(zone.name + " 종합상태");
    heroCircle->setStyleSheet(QString(
        "background-color: qradialgradient(cx:0.5, cy:0.4, radius:0.6, fx:0.5, fy:0.4, stop:0 white, stop:0.15 %1, stop:1 %1);"
        "border-radius:%2px;")
        .arg(color).arg(heroCircle->width() / 2));
    heroStateLabel->setText(textForState(zone.state));
    heroStateLabel->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold;").arg(color));

    tempValueLabel->setText(QString::number(zone.temp, 'f', 1) + " ℃");
    humidityValueLabel->setText(QString::number(zone.humidity, 'f', 0) + " %");
    const double gasPpm = zone.state == ZoneState::Safe ? 3 : (zone.state == ZoneState::Warning ? 8 : 15);
    gasValueLabel->setText(QString::number(gasPpm, 'f', 0) + " ppm");
    smokeValueLabel->setText(zone.state == ZoneState::Danger ? "감지됨" : "미검지");

    for (int i = 0; i < demoStateButtons.size(); ++i)
        demoStateButtons[i]->setChecked(i == int(zone.state));

    for (auto *label : std::as_const(cameraTitleLabels))
        label->setText(label->property("channel").toString() + " - " + zone.name);

    const QVector<double> gasSeries =
        zone.state == ZoneState::Safe ? QVector<double>{ 2, 2.5, 3, 2.8, 3.2, 3 }
        : zone.state == ZoneState::Warning ? QVector<double>{ 3, 4, 6, 8, 7, 6 }
                                            : QVector<double>{ 3, 6, 10, 15, 13, 12 };
    const QVector<double> smokeSeries =
        zone.state == ZoneState::Safe ? QVector<double>{ 5, 8, 10, 9, 7, 6 }
        : zone.state == ZoneState::Warning ? QVector<double>{ 10, 20, 35, 45, 40, 32 }
                                            : QVector<double>{ 15, 35, 60, 85, 78, 70 };

    gasGraph->setData(gasSeries, { "12:00", "23:00" });
    graphTitleLabel->setText("가스 농도 추이 — " + zone.name + " (ppm)");
    smokeGraph->setData(smokeSeries, { "12:00", "23:00" });
    smokeGraphTitleLabel->setText("연기 위험도 추이 — " + zone.name + " (%)");
    eventLogGasGraph->setData(gasSeries, { "12:00", "23:00" });

    manualTitleLabel->setText("수동 제어 — " + zone.name);

    zoneFilterCombo->setCurrentText(zone.name);
}

QLabel *MainWindow::createGlowCircle(int diameter)
{
    auto *circle = new QLabel;
    circle->setFixedSize(diameter, diameter);
    auto *glow = new QGraphicsDropShadowEffect;
    glow->setBlurRadius(40);
    glow->setOffset(0, 0);
    glow->setColor(QColor("#34d399"));
    circle->setGraphicsEffect(glow);
    return circle;
}

QWidget *MainWindow::createMonitoringPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    // hero card
    auto *heroCard = new QFrame;
    heroCard->setFixedWidth(340);
    heroCard->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:10px;").arg(kCardBg, kCardBorder));
    auto *heroLayout = new QVBoxLayout(heroCard);
    heroLayout->setSpacing(10);

    heroTitleLabel = new QLabel(heroCard);
    heroTitleLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    heroTitleLabel->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroTitleLabel);

    heroCircle = createGlowCircle(90);
    auto *circleRow = new QHBoxLayout;
    circleRow->addStretch();
    circleRow->addWidget(heroCircle);
    circleRow->addStretch();
    heroLayout->addLayout(circleRow);

    heroStateLabel = new QLabel(heroCard);
    heroStateLabel->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroStateLabel);
    heroLayout->addSpacing(8);

    auto addStatRow = [&](const QString &name, QLabel **valueOut) {
        auto *row = new QHBoxLayout;
        auto *nameLabel = new QLabel(name, heroCard);
        nameLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
        auto *valueLabel = new QLabel(heroCard);
        valueLabel->setStyleSheet(QString("color:%1; font-weight:bold;").arg(kTextPrimary));
        valueLabel->setAlignment(Qt::AlignRight);
        row->addWidget(nameLabel);
        row->addStretch();
        row->addWidget(valueLabel);
        heroLayout->addLayout(row);
        if (valueOut)
            *valueOut = valueLabel;
    };

    addStatRow("온도", &tempValueLabel);
    addStatRow("습도", &humidityValueLabel);
    addStatRow("가스 농도", &gasValueLabel);
    addStatRow("연기 감지", &smokeValueLabel);
    addStatRow("카메라 상태", &cameraStatusValueLabel);

    heroLayout->addSpacing(8);
    auto *demoLabel = new QLabel("DEMO - 상태 시뮬레이션", heroCard);
    demoLabel->setStyleSheet(QString("color:%1; font-size:11px;").arg(kTextSecondary));
    heroLayout->addWidget(demoLabel);

    auto *demoRow = new QHBoxLayout;
    const QStringList demoNames = { "안전", "경고", "위험" };
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QPushButton(demoNames[i], heroCard);
        btn->setCheckable(true);
        const QString c = colorForState(ZoneState(i));
        btn->setStyleSheet(QString(
            "QPushButton { color:%1; background:transparent; border:1px solid %1; border-radius:6px; padding:4px; }"
            "QPushButton:checked { background-color:%1; color:#0a0a12; font-weight:bold; }").arg(c));
        connect(btn, &QPushButton::clicked, this, [this, i]() { setZoneState(currentZone, ZoneState(i)); });
        demoRow->addWidget(btn);
        demoStateButtons.append(btn);
    }
    heroLayout->addLayout(demoRow);
    heroLayout->addStretch();

    layout->addWidget(heroCard);

    // camera grid
    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    grid->addWidget(createCameraTile(1), 0, 0);
    grid->addWidget(createCameraTile(2), 0, 1);
    grid->addWidget(createCameraTile(3), 1, 0);
    grid->addWidget(createCameraTile(4), 1, 1);
    layout->addLayout(grid, 1);

    return page;
}

QWidget *MainWindow::createCameraTile(int channel)
{
    auto *frame = new QFrame;
    frame->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:8px;").arg(kCardBg, kCardBorder));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(10, 8, 10, 8);

    auto *header = new QHBoxLayout;
    auto *chLabel = new QLabel(QString("Ch.%1 - %2").arg(channel).arg(zones[currentZone].name), frame);
    chLabel->setProperty("channel", QString("Ch.%1").arg(channel));
    chLabel->setStyleSheet(QString("color:%1; font-size:12px;").arg(kTextSecondary));
    header->addWidget(chLabel);
    header->addStretch();
    auto *liveBadge = new QLabel("● LIVE", frame);
    liveBadge->setStyleSheet("color:#f87171; font-size:11px; font-weight:bold;");
    header->addWidget(liveBadge);
    layout->addLayout(header);
    cameraTitleLabels.append(chLabel);

    if (channel == 1) {
        zoneVideoWidget = new QVideoWidget(frame);
        zoneVideoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
        zoneVideoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        zoneVideoWidget->setStyleSheet("background-color:black;");
        layout->addWidget(zoneVideoWidget, 1);
    } else {
        auto *placeholder = new QLabel("영상 연결 스트리밍", frame);
        placeholder->setAlignment(Qt::AlignCenter);
        placeholder->setStyleSheet(QString("color:%1; background-color:#0d0d16;").arg(kTextSecondary));
        layout->addWidget(placeholder, 1);
    }

    return frame;
}

bool MainWindow::showConfirmDialog(const QString &actionName)
{
    QDialog dialog(this);
    dialog.setWindowTitle("조작 확인");
    dialog.setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *header = new QLabel("⚠ 조작 확인", &dialog);
    header->setStyleSheet("color:#fbbf24; font-weight:bold;");
    layout->addWidget(header);

    auto *question = new QLabel(QString("정말 '%1'를 실행하시겠습니까?").arg(actionName), &dialog);
    question->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    question->setWordWrap(true);
    layout->addWidget(question);

    auto *sub = new QLabel("이 작업은 즉시 실행됩니다. 계속하시겠습니까?", &dialog);
    sub->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    layout->addWidget(sub);

    layout->addSpacing(10);
    auto *btnRow = new QHBoxLayout;
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setStyleSheet(QString("QPushButton { background-color:#232333; color:%1; border-radius:6px; padding:10px; }").arg(kTextPrimary));
    auto *execBtn = new QPushButton("실행", &dialog);
    execBtn->setStyleSheet("QPushButton { background-color:#fbbf24; color:#241c00; font-weight:bold; border-radius:6px; padding:10px; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(execBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(execBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    return dialog.exec() == QDialog::Accepted;
}

QWidget *MainWindow::createControlCard(const QString &title, const QString &desc, const std::function<void()> &onConfirm)
{
    auto *card = new QFrame;
    card->setFixedSize(220, 90);
    card->setCursor(Qt::PointingHandCursor);
    card->setStyleSheet(QString("QFrame { background-color:%1; border:1px solid %2; border-radius:8px; }"
                                 "QFrame:hover { border:1px solid %3; }").arg(kCardBg, kCardBorder, kAccent));
    auto *layout = new QVBoxLayout(card);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet(QString("color:%1; font-weight:bold; border:none;").arg(kTextPrimary));
    auto *descLabel = new QLabel(desc, card);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    layout->addWidget(titleLabel);
    layout->addWidget(descLabel);
    layout->addStretch();

    auto *button = new QPushButton(card);
    button->setStyleSheet("background:transparent; border:none;");
    button->setGeometry(card->rect());
    connect(button, &QPushButton::clicked, this, [this, title, onConfirm]() {
        if (showConfirmDialog(title))
            onConfirm();
    });

    return card;
}

QWidget *MainWindow::createManualControlPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(4);

    manualTitleLabel = new QLabel(page);
    manualTitleLabel->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold;").arg(kTextPrimary));
    layout->addWidget(manualTitleLabel);

    auto *subtitle = new QLabel("오작동 방지를 위해 모든 조작은 확인 단계를 거칩니다.", page);
    subtitle->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    layout->addWidget(subtitle);
    layout->addSpacing(16);

    auto *cardRow = new QHBoxLayout;
    cardRow->addWidget(createControlCard("환기팬 가동", "IP66 환기팬 속도 제어", [this]() {
        addEventLog(zones[currentZone].name, "관리자 수동 제어", "환기팬 가동 실행", "admin", "정보", "-", "-");
    }));
    cardRow->addWidget(createControlCard("밸브 개방 / 잠금", "가스 공급 솔레노이드 밸브", [this]() {
        addEventLog(zones[currentZone].name, "관리자 수동 제어", "밸브 개방/잠금 실행", "admin", "정보", "-", "-");
    }));
    cardRow->addWidget(createControlCard("사이렌 끄기", "경보음 및 경고 LED 제어", [this]() {
        addEventLog(zones[currentZone].name, "관리자 수동 제어", "사이렌 끄기 실행", "admin", "정보", "-", "-");
    }));
    cardRow->addStretch();
    layout->addLayout(cardRow);
    layout->addStretch();

    return page;
}

QWidget *MainWindow::createHelpPage()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(10);

    auto *title = new QLabel("도움말", page);
    title->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold;").arg(kTextPrimary));
    layout->addWidget(title);

    const QStringList bullets = {
        "· 종합 상태는 신호등식 3색 인디케이터(안전/경고/위험)로 표시됩니다.",
        "· 경고 단계에서 일정 시간 관리자 대응 시 자동 제어로 전환됩니다.",
        "· 수동 제어 버튼은 오작동 방지를 위해 확인 팝업이 동반됩니다.",
        "· 문의: 보안관제팀 내선 1234",
    };
    for (const QString &b : bullets) {
        auto *label = new QLabel(b, page);
        label->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
        layout->addWidget(label);
    }
    layout->addStretch();
    return page;
}

QWidget *MainWindow::createGraphPage()
{
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    auto *gasCol = new QVBoxLayout;
    graphTitleLabel = new QLabel(page);
    graphTitleLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    gasCol->addWidget(graphTitleLabel);
    gasCol->addSpacing(8);
    gasGraph = new GasGraphWidget(page);
    gasGraph->setLineColor(QColor(kAccent));
    gasCol->addWidget(gasGraph, 1);
    layout->addLayout(gasCol);

    auto *smokeCol = new QVBoxLayout;
    smokeGraphTitleLabel = new QLabel(page);
    smokeGraphTitleLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    smokeCol->addWidget(smokeGraphTitleLabel);
    smokeCol->addSpacing(8);
    smokeGraph = new GasGraphWidget(page);
    smokeGraph->setLineColor(QColor("#fb923c"));
    smokeCol->addWidget(smokeGraph, 1);
    layout->addLayout(smokeCol);

    return page;
}

QWidget *MainWindow::createEventLogPage()
{
    auto *page = new QWidget;
    auto *mainLayout = new QHBoxLayout(page);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // left column: filter + log list (top), gas graph (bottom)
    auto *leftCol = new QVBoxLayout;

    auto *filterBar = new QHBoxLayout;
    zoneFilterCombo = new QComboBox(page);
    zoneFilterCombo->addItems(kZoneFilterNames);
    auto *zoneLabel = new QLabel("구역:", page);
    zoneLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    filterBar->addWidget(zoneLabel);
    filterBar->addWidget(zoneFilterCombo);

    searchEdit = new QLineEdit(page);
    searchEdit->setPlaceholderText("감지 내용 검색...");
    filterBar->addWidget(searchEdit, 1);

    auto *searchBtn = new QPushButton("조회", page);
    searchBtn->setStyleSheet(QString("background-color:%1; color:white; border-radius:6px; padding:6px 14px;").arg(kAccent));
    filterBar->addWidget(searchBtn);
    leftCol->addLayout(filterBar);
    leftCol->addSpacing(8);

    eventTable = new QTableWidget(0, 4, page);
    eventTable->setHorizontalHeaderLabels({ "시간", "구역", "감지 내용", "대응 결과" });
    eventTable->horizontalHeader()->setStretchLastSection(true);
    eventTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    eventTable->setStyleSheet(QString(
        "QTableWidget { background-color:%1; color:%2; border:1px solid %3; gridline-color:%3; }"
        "QHeaderView::section { background-color:#1a1a26; color:%4; border:none; padding:6px; }")
        .arg(kCardBg, kTextPrimary, kCardBorder, kTextSecondary));
    leftCol->addWidget(eventTable, 3);

    auto *graphLabel = new QLabel(page);
    graphLabel->setText("가스 농도 추이");
    graphLabel->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold;").arg(kTextPrimary));
    leftCol->addSpacing(8);
    leftCol->addWidget(graphLabel);
    eventLogGasGraph = new GasGraphWidget(page);
    eventLogGasGraph->setLineColor(QColor(kAccent));
    leftCol->addWidget(eventLogGasGraph, 2);

    mainLayout->addLayout(leftCol, 2);

    // right column: detail panel
    auto *detailFrame = new QFrame(page);
    detailFrame->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:8px;").arg(kCardBg, kCardBorder));
    auto *detailOuter = new QVBoxLayout(detailFrame);

    detailPlaceholder = new QLabel("로그를 선택하면 세부 내용이 표시됩니다.", detailFrame);
    detailPlaceholder->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    detailPlaceholder->setAlignment(Qt::AlignCenter);
    detailOuter->addWidget(detailPlaceholder);

    detailContent = new QWidget(detailFrame);
    auto *detailLayout = new QVBoxLayout(detailContent);

    auto addDetailRow = [&](const QString &name, QLabel **valueOut) {
        auto *row = new QHBoxLayout;
        auto *nameLabel = new QLabel(name, detailContent);
        nameLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
        nameLabel->setFixedWidth(90);
        auto *valueLabel = new QLabel(detailContent);
        valueLabel->setStyleSheet(QString("color:%1; font-weight:bold;").arg(kTextPrimary));
        valueLabel->setWordWrap(true);
        row->addWidget(nameLabel);
        row->addWidget(valueLabel, 1);
        detailLayout->addLayout(row);
        if (valueOut)
            *valueOut = valueLabel;
    };

    addDetailRow("발생 시간", &detailTimeValue);
    addDetailRow("구역", &detailZoneValue);
    addDetailRow("관리자", &detailAdminValue);
    addDetailRow("이벤트 유형", &detailTypeValue);
    addDetailRow("위험도 단계", &detailSeverityValue);
    addDetailRow("트리거 센서", &detailSensorValue);
    addDetailRow("자동 대응", &detailResponseValue);
    addDetailRow("처리 상태", &detailStatusValue);
    addDetailRow("지속 시간", &detailDurationValue);

    detailLayout->addSpacing(8);
    auto *snapshotLabel = new QLabel("연관 영상 스냅샷", detailContent);
    snapshotLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    detailLayout->addWidget(snapshotLabel);
    auto *snapshotBox = new QFrame(detailContent);
    snapshotBox->setFixedHeight(110);
    snapshotBox->setStyleSheet("background-color:#0d0d16; border:1px dashed #333;");
    auto *snapshotBoxLayout = new QVBoxLayout(snapshotBox);
    auto *snapshotText = new QLabel("스냅샷 없음", snapshotBox);
    snapshotText->setAlignment(Qt::AlignCenter);
    snapshotText->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
    snapshotBoxLayout->addWidget(snapshotText);
    detailLayout->addWidget(snapshotBox);

    detailLayout->addSpacing(8);
    falseAlarmButton = new QPushButton("오탐 신고", detailContent);
    falseAlarmButton->setStyleSheet(QString(
        "QPushButton { background-color:#232333; color:%1; border-radius:6px; padding:8px; }"
        "QPushButton:hover { background-color:#2c2c40; }").arg(kTextPrimary));
    connect(falseAlarmButton, &QPushButton::clicked, this, &MainWindow::markFalseAlarm);
    detailLayout->addWidget(falseAlarmButton);
    detailLayout->addStretch();

    detailContent->hide();
    detailOuter->addWidget(detailContent);

    mainLayout->addWidget(detailFrame, 1);

    connect(searchBtn, &QPushButton::clicked, this, &MainWindow::applyEventFilter);
    connect(zoneFilterCombo, &QComboBox::currentIndexChanged, this, &MainWindow::applyEventFilter);
    connect(searchEdit, &QLineEdit::returnPressed, this, &MainWindow::applyEventFilter);
    connect(eventTable, &QTableWidget::cellClicked, this, &MainWindow::showEventDetail);

    return page;
}

void MainWindow::addEventLog(const QString &zone, const QString &detection, const QString &response,
                              const QString &admin, const QString &severity,
                              const QString &sensorCombo, const QString &duration)
{
    EventEntry entry;
    entry.time = QDateTime::currentDateTime().toString("HH:mm:ss");
    entry.zone = zone;
    entry.detection = detection;
    entry.response = response;
    entry.admin = admin;
    entry.severity = severity;
    entry.sensorCombo = sensorCombo;
    entry.status = "해결됨";
    entry.duration = duration;
    eventEntries.append(entry);

    const int row = eventTable->rowCount();
    eventTable->insertRow(row);
    eventTable->setItem(row, 0, new QTableWidgetItem(entry.time));
    eventTable->setItem(row, 1, new QTableWidgetItem(entry.zone));
    eventTable->setItem(row, 2, new QTableWidgetItem(entry.detection));
    eventTable->setItem(row, 3, new QTableWidgetItem(entry.response));
    eventTable->scrollToBottom();
}

void MainWindow::showEventDetail(int row, int)
{
    if (row < 0 || row >= eventEntries.size())
        return;
    selectedEventRow = row;
    const EventEntry &entry = eventEntries[row];

    detailPlaceholder->hide();
    detailContent->show();

    detailTimeValue->setText(entry.time);
    detailZoneValue->setText(entry.zone);
    detailAdminValue->setText(entry.admin);
    detailTypeValue->setText(entry.detection);
    detailSeverityValue->setText(entry.severity);
    detailSensorValue->setText(entry.sensorCombo);
    detailResponseValue->setText(entry.response);
    detailStatusValue->setText(entry.status);
    detailDurationValue->setText(entry.duration);
}

void MainWindow::markFalseAlarm()
{
    if (selectedEventRow < 0 || selectedEventRow >= eventEntries.size())
        return;
    eventEntries[selectedEventRow].status = "오탐 처리됨";
    detailStatusValue->setText("오탐 처리됨");
}

void MainWindow::applyEventFilter()
{
    const QString zone = zoneFilterCombo->currentText();
    const QString keyword = searchEdit->text().trimmed();

    for (int row = 0; row < eventTable->rowCount(); ++row) {
        const bool zoneMatch = (zone == "전체") || (eventTable->item(row, 1)->text() == zone);
        const bool keywordMatch = keyword.isEmpty()
            || eventTable->item(row, 2)->text().contains(keyword, Qt::CaseInsensitive)
            || eventTable->item(row, 3)->text().contains(keyword, Qt::CaseInsensitive);
        eventTable->setRowHidden(row, !(zoneMatch && keywordMatch));
    }
}
