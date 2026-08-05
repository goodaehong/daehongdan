#include "StatusPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QTimer>
#include <QScrollArea>
#include <QDialog>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#aca6c2";
const QString kCardBg = "#1a1a26";
const QString kCardBorder = "#232333";
const QString kSafeColor = "#34d399";
const QString kWarnColor = "#fbbf24";
const QString kDangerColor = "#f87171";

struct Trend { QString text; QString color; };

// 최근 값과 그보다 조금 이전 구간을 비교해 상승/하강/안정 판정. 이력이 짧으면(4개 미만) 빈 값.
Trend trendFor(const QVector<double> &history)
{
    if (history.size() < 4)
        return { QString(), kTextSecondary };
    const int n = history.size();
    const double recent = (history[n - 1] + history[n - 2]) / 2.0;
    const int priorIdx = qMax(0, n - 6);
    const double prior = (history[priorIdx] + history[qMin(n - 1, priorIdx + 1)]) / 2.0;
    const double diff = recent - prior;
    const double scale = qMax(1.0, qAbs(prior));
    if (diff > scale * 0.05)
        return { "▲ 상승", kWarnColor };
    if (diff < -scale * 0.05)
        return { "▼ 하강", kSafeColor };
    return { "● 안정", kTextSecondary };
}
}

// 임계값 대비 현재 값 비율을 색깔 있는 얇은 막대로 보여주는 미니 게이지.
class GaugeBar : public QWidget
{
public:
    explicit GaugeBar(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(6);
        setMinimumWidth(40);
    }

    void setRatio(double ratio, const QColor &color)
    {
        m_ratio = qBound(0.0, ratio, 1.0);
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#2a2a3a"));
        p.drawRoundedRect(QRectF(0, 0, width(), height()), height() / 2.0, height() / 2.0);
        if (m_ratio > 0) {
            p.setBrush(m_color);
            p.drawRoundedRect(QRectF(0, 0, width() * m_ratio, height()), height() / 2.0, height() / 2.0);
        }
    }

private:
    double m_ratio = 0;
    QColor m_color = QColor(kSafeColor);
};

StatusPanel::StatusPanel(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(420);
    setStyleSheet("background-color:#14141f; border:1px solid #232333; border-radius:10px;");

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // 카드가 많아져 세로 공간이 모자랄 수 있어(작은 창/DPI 등) 스크롤로 감싼다.
    // 이렇게 하면 레이아웃이 최소 크기 밑으로 강제로 눌려 겹쳐 보이는 일이 없다.
    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("background:transparent; border:none;");
    outerLayout->addWidget(scrollArea);

    auto *contentWidget = new QWidget(scrollArea);
    contentWidget->setStyleSheet("background:transparent;");
    scrollArea->setWidget(contentWidget);

    auto *heroLayout = new QVBoxLayout(contentWidget);
    heroLayout->setContentsMargins(16, 12, 16, 12);
    heroLayout->setSpacing(9);

    heroTitleLabel = new QLabel(contentWidget);
    heroTitleLabel->setStyleSheet(QString("color:%1; font-size:14px; font-weight:bold; border:none;").arg(kTextSecondary));
    heroTitleLabel->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroTitleLabel);

    heroCircle = new QLabel(contentWidget);
    heroCircle->setFixedSize(86, 86);
    auto *glow = new QGraphicsDropShadowEffect;
    glow->setBlurRadius(36);
    glow->setOffset(0, 0);
    glow->setColor(QColor("#34d399"));
    heroCircle->setGraphicsEffect(glow);

    auto *circleRow = new QHBoxLayout;
    circleRow->addStretch();
    circleRow->addWidget(heroCircle);
    circleRow->addStretch();
    heroLayout->addLayout(circleRow);

    heroStateLabel = new QLabel(contentWidget);
    heroStateLabel->setAlignment(Qt::AlignCenter);
    heroStateLabel->setStyleSheet("border:none;");
    heroLayout->addWidget(heroStateLabel);

    heroCauseLabel = new QLabel(contentWidget);
    heroCauseLabel->setAlignment(Qt::AlignCenter);
    heroCauseLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    heroLayout->addWidget(heroCauseLabel);

    heroElapsedLabel = new QLabel(contentWidget);
    heroElapsedLabel->setAlignment(Qt::AlignCenter);
    heroElapsedLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    heroLayout->addWidget(heroElapsedLabel);

    elapsedTimer = new QTimer(this);
    connect(elapsedTimer, &QTimer::timeout, this, &StatusPanel::updateElapsedLabel);
    elapsedTimer->start(1000);

    auto makeCard = [&](const QString &sectionTitle) {
        auto *card = new QFrame(contentWidget);
        card->setStyleSheet(QString("QFrame { background-color:%1; border:1px solid %2; border-radius:8px; }").arg(kCardBg, kCardBorder));
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(14, 12, 14, 12);
        cardLayout->setSpacing(8);
        auto *header = new QLabel(sectionTitle, card);
        header->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold; letter-spacing:1px; border:none;").arg(kTextSecondary));
        cardLayout->addWidget(header);
        heroLayout->addWidget(card);
        return cardLayout;
    };

    auto addGaugeRow = [&](QVBoxLayout *cardLayout, QWidget *cardWidget, const QString &name,
                            const QString &thresholdText, QLabel **valueOut, GaugeBar **gaugeOut, QLabel **trendOut) {
        auto *topRow = new QHBoxLayout;
        auto *nameLabel = new QLabel(name, cardWidget);
        nameLabel->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
        auto *valueLabel = new QLabel(cardWidget);
        valueLabel->setStyleSheet(QString("color:%1; font-size:17px; font-weight:bold; border:none;").arg(kTextPrimary));
        valueLabel->setAlignment(Qt::AlignRight);
        topRow->addWidget(nameLabel);
        topRow->addStretch();
        topRow->addWidget(valueLabel);
        cardLayout->addLayout(topRow);

        auto *gauge = new GaugeBar(cardWidget);
        cardLayout->addWidget(gauge);

        auto *bottomRow = new QHBoxLayout;
        auto *trendLabel = new QLabel(cardWidget);
        trendLabel->setStyleSheet(QString("font-size:11px; border:none; color:%1;").arg(kTextSecondary));
        auto *thresholdLabel = new QLabel(thresholdText, cardWidget);
        thresholdLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
        bottomRow->addWidget(trendLabel);
        bottomRow->addStretch();
        bottomRow->addWidget(thresholdLabel);
        cardLayout->addLayout(bottomRow);

        if (valueOut) *valueOut = valueLabel;
        if (gaugeOut) *gaugeOut = gauge;
        if (trendOut) *trendOut = trendLabel;
    };

    QVBoxLayout *sensorLayout = makeCard("위험 감지 센서");
    QWidget *sensorCardWidget = sensorLayout->parentWidget();
    addGaugeRow(sensorLayout, sensorCardWidget, "가스 농도", "임계 2000ppm", &gasValueLabel, &gasGaugeBar, &gasTrendLabel);
    addGaugeRow(sensorLayout, sensorCardWidget, "불꽃 센서", "임계 1.0V", &flameValueLabel, &flameGaugeBar, &flameTrendLabel);

    auto *smokeRow = new QHBoxLayout;
    auto *smokeName = new QLabel("연기 감지", sensorCardWidget);
    smokeName->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
    smokeBadgeLabel = new QLabel(sensorCardWidget);
    smokeRow->addWidget(smokeName);
    smokeRow->addStretch();
    smokeRow->addWidget(smokeBadgeLabel);
    sensorLayout->addLayout(smokeRow);

    smokeGaugeBar = new GaugeBar(sensorCardWidget);
    sensorLayout->addWidget(smokeGaugeBar);

    smokeHistoryLabel = new QLabel(sensorCardWidget);
    smokeHistoryLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    smokeHistoryLabel->setAlignment(Qt::AlignRight);
    sensorLayout->addWidget(smokeHistoryLabel);

    QVBoxLayout *envLayout = makeCard("환경");
    QWidget *envCardWidget = envLayout->parentWidget();
    auto *envRow = new QHBoxLayout;
    envRow->setSpacing(6);
    auto *tempNameLabel = new QLabel("온도", envCardWidget);
    tempNameLabel->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
    tempValueLabel = new QLabel(envCardWidget);
    auto *humidityNameLabel = new QLabel("습도", envCardWidget);
    humidityNameLabel->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
    humidityValueLabel = new QLabel(envCardWidget);
    envRow->addWidget(tempNameLabel);
    envRow->addWidget(tempValueLabel);
    envRow->addSpacing(14);
    envRow->addWidget(humidityNameLabel);
    envRow->addWidget(humidityValueLabel);
    envRow->addStretch();
    envLayout->addLayout(envRow);

    QVBoxLayout *cameraLayout = makeCard("카메라 채널");
    QWidget *cameraCardWidget = cameraLayout->parentWidget();
    cameraHeaderLabel = new QLabel(cameraCardWidget);
    cameraHeaderLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    cameraLayout->addWidget(cameraHeaderLabel);
    auto *cameraGrid = new QGridLayout;
    cameraGrid->setHorizontalSpacing(8);
    cameraGrid->setVerticalSpacing(8);
    for (int i = 0; i < 4; ++i) {
        auto *cellFrame = new QFrame(cameraCardWidget);
        cellFrame->setStyleSheet(QString("QFrame { border:1px solid %1; border-radius:6px; }").arg(kCardBorder));
        auto *cell = new QHBoxLayout(cellFrame);
        cell->setContentsMargins(8, 5, 8, 5);
        cell->setSpacing(6);
        auto *dot = new QLabel("●", cellFrame);
        dot->setStyleSheet("color:#6b7280; font-size:12px; border:none;");
        auto *nameLabel = new QLabel(QString("Ch.%1 · %2").arg(i + 1).arg(channelTargetName(i + 1)), cellFrame);
        nameLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextPrimary));
        cell->addWidget(dot);
        cell->addWidget(nameLabel);
        cell->addStretch();
        cameraGrid->addWidget(cellFrame, i / 2, i % 2);
        channelDotLabels[i] = dot;
        channelFrames[i] = cellFrame;
    }
    cameraGrid->setColumnStretch(0, 1);
    cameraGrid->setColumnStretch(1, 1);
    cameraLayout->addLayout(cameraGrid);
    refreshCameraHeader();

    QVBoxLayout *actuatorLayout = makeCard("액추에이터 상태");
    QWidget *actuatorCardWidget = actuatorLayout->parentWidget();

    auto *actuatorHeaderRow = new QHBoxLayout;
    auto *realtimeLabel = new QLabel("실시간", actuatorCardWidget);
    realtimeLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    actuatorLinkLabel = new QLabel(actuatorCardWidget);
    actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    actuatorLinkLabel->setText("● 확인 중");
    actuatorHeaderRow->addWidget(realtimeLabel);
    actuatorHeaderRow->addStretch();
    actuatorHeaderRow->addWidget(actuatorLinkLabel);
    actuatorLayout->addLayout(actuatorHeaderRow);

    // 자동/수동 모드는 서버가 액추에이터별로 따로 보내주기로 했으나 아직 미배포 -> 지금은 각 줄에
    // "확인 중" 자리표시만 두고, 필드가 오면 setActuatorStatus에서 채운다.
    auto addPillRow = [&](QVBoxLayout *cardLayout, QWidget *cardWidget, const QString &name,
                           QLabel **valueOut, QLabel **modeOut) {
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        auto *nameLabel = new QLabel(name, cardWidget);
        nameLabel->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
        auto *modeLabel = new QLabel("확인 중", cardWidget);
        modeLabel->setStyleSheet(QString(
            "background-color:transparent; color:%1; font-size:10px; border-radius:8px; padding:2px 6px; border:1px solid %1;")
            .arg(kTextSecondary));
        auto *valueLabel = new QLabel(cardWidget);
        row->addWidget(nameLabel);
        row->addWidget(modeLabel);
        row->addStretch();
        row->addWidget(valueLabel);
        cardLayout->addLayout(row);
        if (valueOut) *valueOut = valueLabel;
        if (modeOut) *modeOut = modeLabel;
    };
    addPillRow(actuatorLayout, actuatorCardWidget, "환기팬", &fanValueLabel, &fanModeLabel);
    addPillRow(actuatorLayout, actuatorCardWidget, "밸브", &valveValueLabel, &valveModeLabel);
    addPillRow(actuatorLayout, actuatorCardWidget, "사이렌", &sirenValueLabel, &sirenModeLabel);

    // ── 수동 제어 카드: 카메라 영상이 보이는 이 화면 안에서 바로 조작할 수 있도록 배치.
    QVBoxLayout *controlLayout = makeCard("수동 제어");
    QWidget *controlCardWidget = controlLayout->parentWidget();

    commandStatusLabel = new QLabel(controlCardWidget);
    commandStatusLabel->setWordWrap(true);
    commandStatusLabel->setVisible(false);
    controlLayout->addWidget(commandStatusLabel);

    auto addControlRow = [&](const QString &rowLabel, const QStringList &optionLabels,
                              const QStringList &optionActions, const QString &target,
                              QVector<QPushButton *> &outButtons) {
        auto *nameLabel = new QLabel(rowLabel, controlCardWidget);
        nameLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
        controlLayout->addWidget(nameLabel);
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        for (int i = 0; i < optionLabels.size(); ++i) {
            auto *btn = new QPushButton(optionLabels[i], controlCardWidget);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setFixedHeight(30);
            const QString title = rowLabel + " " + optionLabels[i];
            const QString action = optionActions[i];
            connect(btn, &QPushButton::clicked, this, [this, target, action, title]() {
                if (showConfirmDialog(title))
                    emit controlActionRequested(target, action, title);
            });
            row->addWidget(btn);
            outButtons.append(btn);
        }
        controlLayout->addLayout(row);
    };

    addControlRow("환기팬", { "OFF", "약", "중", "강" }, { "off", "low", "mid", "high" }, "fan", fanCtrlButtons);
    addControlRow("밸브", { "잠금", "개방" }, { "close", "open" }, "valve", valveCtrlButtons);
    addControlRow("사이렌", { "OFF", "ON" }, { "off", "on" }, "siren", sirenCtrlButtons);

    auto *evacBtn = new QPushButton("대피 모드 발동", controlCardWidget);
    evacBtn->setCursor(Qt::PointingHandCursor);
    evacBtn->setFixedHeight(36);
    evacBtn->setStyleSheet(
        "QPushButton { background-color:#ef4444; color:white; border:none; border-radius:6px; font-size:13px; font-weight:bold; }"
        "QPushButton:hover { background-color:#dc2626; }");
    connect(evacBtn, &QPushButton::clicked, this, [this]() {
        // 전 구역에 영향을 주고 되돌리기 어려운 조작이라 1번이 아닌 2단계로 확인한다.
        if (showConfirmDialog("대피 모드 발동") && showEvacuationConfirmDialog())
            emit controlActionRequested("evacuation", "trigger", "대피 모드 발동");
    });
    controlLayout->addSpacing(2);
    controlLayout->addWidget(evacBtn);

    setActuatorStatus(-1, -1, -1, "", "", "", ""); // 서버 응답 오기 전 초기 표시

    auto *demoLabel = new QLabel("DEMO - 상태 시뮬레이션", contentWidget);
    demoLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    heroLayout->addWidget(demoLabel);

    auto *demoRow = new QHBoxLayout;
    demoRow->setSpacing(8);
    const QStringList demoNames = { "안전", "경고", "위험" };
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QPushButton(demoNames[i], contentWidget);
        btn->setCheckable(true);
        const QString c = colorForState(ZoneState(i));
        btn->setStyleSheet(QString(
            "QPushButton { color:%1; background:transparent; border:1px solid %1; border-radius:6px; padding:7px; font-size:14px; }"
            "QPushButton:checked { background-color:%1; color:#0a0a12; font-weight:bold; }").arg(c));
        connect(btn, &QPushButton::clicked, this, [this, i]() { emit demoStateRequested(ZoneState(i)); });
        demoRow->addWidget(btn);
        demoStateButtons.append(btn);
    }
    heroLayout->addLayout(demoRow);
    heroLayout->addStretch();
}

QString StatusPanel::pillStyle(const QString &color, bool filled) const
{
    if (filled)
        return QString("background-color:%1; color:#0a0a12; font-size:12px; font-weight:bold; "
                        "border-radius:9px; padding:3px 10px; border:none;").arg(color);
    return QString("background-color:transparent; color:%1; font-size:12px; font-weight:bold; "
                    "border-radius:9px; padding:3px 10px; border:1px solid %1;").arg(color);
}

void StatusPanel::updateElapsedLabel()
{
    if (!stateEnteredAt.isValid()) {
        heroElapsedLabel->clear();
        return;
    }
    const qint64 secs = stateEnteredAt.secsTo(QDateTime::currentDateTime());
    if (secs < 0) {
        heroElapsedLabel->clear();
        return;
    }
    const qint64 m = secs / 60;
    const qint64 s = secs % 60;
    heroElapsedLabel->setText(QString("%1:%2 경과")
        .arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0')));
}

void StatusPanel::refreshCameraHeader()
{
    int connectedCount = 0;
    for (bool c : channelConnected)
        if (c) ++connectedCount;
    cameraHeaderLabel->setText(QString("%1/4 연결").arg(connectedCount));
}

void StatusPanel::setCameraChannelStatus(int channel, bool connected)
{
    const int index = channel - 1;
    if (index < 0 || index >= 4)
        return;
    channelConnected[index] = connected;
    const QString dotColor = connected ? kSafeColor : kDangerColor;
    channelDotLabels[index]->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(dotColor));
    channelFrames[index]->setStyleSheet(QString("QFrame { border:1px solid %1; border-radius:6px; }").arg(dotColor));
    refreshCameraHeader();
}

void StatusPanel::updateZone(const Zone &zone)
{
    const QString color = colorForState(zone.state);

    heroTitleLabel->setText(zone.name + " 종합상태");
    heroCircle->setStyleSheet(QString(
        "background-color: qradialgradient(cx:0.5, cy:0.4, radius:0.6, fx:0.5, fy:0.4, stop:0 white, stop:0.15 %1, stop:1 %1);"
        "border-radius:%2px;")
        .arg(color).arg(heroCircle->width() / 2));
    heroStateLabel->setText(textForState(zone.state));
    heroStateLabel->setStyleSheet(QString("color:%1; font-size:21px; font-weight:bold; border:none;").arg(color));

    const QString causePhrase = causeText(zone.cause);
    heroCauseLabel->setText(causePhrase.isEmpty() ? "이상 없음" : causePhrase);
    heroCauseLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;")
        .arg(causePhrase.isEmpty() ? kTextSecondary : color));

    stateEnteredAt = zone.stateEnteredAt;
    updateElapsedLabel();

    // "자동(평상시)" vs "자동(위험)" 구분 기준이 바뀔 수 있으니 구역 상태가 갱신될 때마다 다시 그린다.
    lastKnownZoneState = zone.state;
    updateModeLabel(fanModeLabel, lastFanSrc);
    updateModeLabel(valveModeLabel, lastValveSrc);
    updateModeLabel(sirenModeLabel, lastSirenSrc);

    // 배경 알약이 아니라 글자 색만 눈에 띄게 칠한다.
    tempValueLabel->setText(QString("%1℃").arg(QString::number(zone.temp, 'f', 1)));
    tempValueLabel->setStyleSheet("color:#60a5fa; font-size:16px; font-weight:bold; border:none;");
    humidityValueLabel->setText(QString("%1%").arg(QString::number(zone.humidity, 'f', 1)));
    humidityValueLabel->setStyleSheet("color:#22d3ee; font-size:16px; font-weight:bold; border:none;");

    // 서버 임계값: 가스 경고 200ppm / 위험 2000ppm (server/judgement.cpp GAS_WARN_THRESHOLD/GAS_DANGER_THRESHOLD)
    constexpr double kGasWarn = 200, kGasDanger = 2000, kGasScale = 2000;
    // 불꽃센서 위험 임계값 1.0V (server/judgement.cpp FLAME_THRESHOLD)
    constexpr double kFlameThreshold = 1.0, kFlameScale = 3.0;
    // MQ-2 연기 위험 임계값 150ppm (server/judgement.cpp SMOKE_THRESHOLD)
    constexpr double kSmokeThreshold = 150, kSmokeScale = 300;

    double gasVal, flameVal, smokeVal;
    bool smokeDetected;
    if (zone.hasLiveSensorData) {
        gasVal = zone.gasPpm;
        flameVal = zone.flameVal;
        smokeVal = zone.smokePpm;
        smokeDetected = zone.smokePpm > kSmokeThreshold;
    } else {
        // DEMO 시뮬레이션(실센서 없는 구역): 상태에 따른 가짜 값
        gasVal = zone.state == ZoneState::Safe ? 35 : (zone.state == ZoneState::Warning ? 180 : 1800);
        flameVal = zone.state == ZoneState::Safe ? 0.12 : (zone.state == ZoneState::Warning ? 0.5 : 2.5);
        smokeVal = zone.state == ZoneState::Safe ? 20 : (zone.state == ZoneState::Warning ? 80 : 300);
        smokeDetected = zone.state == ZoneState::Danger;
    }

    gasValueLabel->setText(QString::number(gasVal, 'f', 0) + " ppm");
    const QColor gasColor = gasVal >= kGasDanger ? QColor(kDangerColor)
                             : gasVal >= kGasWarn ? QColor(kWarnColor) : QColor(kSafeColor);
    gasGaugeBar->setRatio(gasVal / kGasScale, gasColor);
    const Trend gasTrend = trendFor(zone.gasHistory);
    gasTrendLabel->setText(gasTrend.text);
    gasTrendLabel->setStyleSheet(QString("font-size:11px; border:none; color:%1;").arg(gasTrend.color));

    flameValueLabel->setText(QString::number(flameVal, 'f', 2) + " V");
    flameValueLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; border:none;")
        .arg(flameVal > kFlameThreshold ? kDangerColor : kTextPrimary));
    const QColor flameColor = flameVal > kFlameThreshold ? QColor(kDangerColor) : QColor(kSafeColor);
    flameGaugeBar->setRatio(flameVal / kFlameScale, flameColor);
    const Trend flameTrend = trendFor(zone.flameHistory);
    flameTrendLabel->setText(flameTrend.text);
    flameTrendLabel->setStyleSheet(QString("font-size:11px; border:none; color:%1;").arg(flameTrend.color));

    smokeBadgeLabel->setText(smokeDetected ? "감지됨" : "미검지");
    smokeBadgeLabel->setStyleSheet(pillStyle(smokeDetected ? kDangerColor : kSafeColor, true));
    smokeGaugeBar->setRatio(smokeVal / kSmokeScale, smokeDetected ? QColor(kDangerColor) : QColor(kSafeColor));
    int smokeDetectedCount = 0;
    for (bool b : zone.smokeDetectHistory)
        if (b) ++smokeDetectedCount;
    smokeHistoryLabel->setText(zone.smokeDetectHistory.isEmpty() ? QString()
        : QString("최근 %1회 판정 · 감지 %2회").arg(zone.smokeDetectHistory.size()).arg(smokeDetectedCount));

    for (int i = 0; i < demoStateButtons.size(); ++i)
        demoStateButtons[i]->setChecked(i == int(zone.state));
}

void StatusPanel::setActuatorStatus(int fan, int valve, int siren, const QString &link,
                                     const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc)
{
    if (link == "ok") {
        actuatorLinkLabel->setText("● 연결됨");
        actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kSafeColor));
    } else if (link == "down") {
        actuatorLinkLabel->setText("● 연결 끊김");
        actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kDangerColor));
    } else {
        actuatorLinkLabel->setText("● 확인 중");
        actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    }

    lastFanSrc = fanSrc;
    lastValveSrc = valveSrc;
    lastSirenSrc = sirenSrc;
    updateModeLabel(fanModeLabel, lastFanSrc);
    updateModeLabel(valveModeLabel, lastValveSrc);
    updateModeLabel(sirenModeLabel, lastSirenSrc);

    static const QStringList kFanLabels = { "OFF", "약", "중", "강" };
    // 세기별로 색을 다르게(약=파랑 → 중=노랑 → 강=빨강), OFF만 회색 아웃라인.
    static const QStringList kFanColors = { kTextSecondary, "#60a5fa", kWarnColor, kDangerColor };
    if (fan >= 0 && fan < kFanLabels.size()) {
        fanValueLabel->setText(kFanLabels[fan]);
        fanValueLabel->setStyleSheet(pillStyle(kFanColors[fan], fan != 0));
    } else {
        fanValueLabel->setText("확인 중");
        fanValueLabel->setStyleSheet(pillStyle(kTextSecondary, false));
    }

    if (valve == 0 || valve == 1) {
        valveValueLabel->setText(valve == 1 ? "개방" : "잠금");
        valveValueLabel->setStyleSheet(pillStyle(valve == 1 ? kWarnColor : kSafeColor, true));
    } else {
        valveValueLabel->setText("확인 중");
        valveValueLabel->setStyleSheet(pillStyle(kTextSecondary, false));
    }

    if (siren == 0 || siren == 1) {
        sirenValueLabel->setText(siren == 1 ? "ON" : "OFF");
        sirenValueLabel->setStyleSheet(pillStyle(siren == 1 ? kDangerColor : kTextSecondary, siren == 1));
    } else {
        sirenValueLabel->setText("확인 중");
        sirenValueLabel->setStyleSheet(pillStyle(kTextSecondary, false));
    }

    updateControlButtonStyles(fanCtrlButtons, fan);
    updateControlButtonStyles(valveCtrlButtons, valve);
    updateControlButtonStyles(sirenCtrlButtons, siren);
}

void StatusPanel::updateModeLabel(QLabel *label, const QString &source)
{
    if (source == "manual") {
        label->setText("수동 개입");
        label->setStyleSheet(
            "background-color:transparent; color:#f59e0b; font-size:10px; border-radius:8px; padding:2px 6px; border:1px solid #f59e0b;");
    } else if (source == "auto") {
        const bool emergency = lastKnownZoneState != ZoneState::Safe;
        label->setText(emergency ? "자동(위험)" : "자동(평상시)");
        const QString color = emergency ? kDangerColor : kSafeColor;
        label->setStyleSheet(QString(
            "background-color:transparent; color:%1; font-size:10px; border-radius:8px; padding:2px 6px; border:1px solid %1;").arg(color));
    } else {
        label->setText("확인 중");
        label->setStyleSheet(QString(
            "background-color:transparent; color:%1; font-size:10px; border-radius:8px; padding:2px 6px; border:1px solid %1;").arg(kTextSecondary));
    }
}

void StatusPanel::setActuatorRowStatus(const QString &target, const QString &text, const QString &color)
{
    QLabel *label = nullptr;
    if (target == "fan") label = fanValueLabel;
    else if (target == "valve") label = valveValueLabel;
    else if (target == "siren") label = sirenValueLabel;
    if (!label)
        return; // evacuation 등 액추에이터 상태 줄이 없는 target은 무시
    label->setText(text);
    label->setStyleSheet(pillStyle(color, true));
}

void StatusPanel::updateControlButtonStyles(QVector<QPushButton *> &buttons, int activeIndex)
{
    for (int i = 0; i < buttons.size(); ++i) {
        const bool active = (i == activeIndex);
        buttons[i]->setStyleSheet(active
            ? "QPushButton { background-color:#8b7cf6; color:white; border:none; border-radius:6px; font-size:13px; font-weight:bold; }"
            : QString("QPushButton { background-color:#232333; color:%1; border:none; border-radius:6px; font-size:13px; }").arg(kTextSecondary));
    }
}

void StatusPanel::showCommandStatus(const QString &text, const QString &color)
{
    commandStatusLabel->setText(text);
    commandStatusLabel->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold; border:none;").arg(color));
    commandStatusLabel->setVisible(!text.isEmpty());

    if (!statusClearTimer) {
        statusClearTimer = new QTimer(this);
        statusClearTimer->setSingleShot(true);
        connect(statusClearTimer, &QTimer::timeout, this, [this]() {
            commandStatusLabel->clear();
            commandStatusLabel->setVisible(false);
        });
    }
    statusClearTimer->start(4000); // 몇 초 후 자동으로 사라짐 (다음 명령이 오면 다시 갱신됨)
}

bool StatusPanel::showConfirmDialog(const QString &actionName)
{
    QDialog dialog(this);
    dialog.setWindowTitle("조작 확인");
    dialog.setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    dialog.setMinimumWidth(440);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(14);

    auto *header = new QLabel("⚠ 조작 확인", &dialog);
    header->setStyleSheet("color:#fbbf24; font-size:18px; font-weight:bold; border:none;");
    layout->addWidget(header);

    auto *question = new QLabel(QString("정말 '%1'를 실행하시겠습니까?").arg(actionName), &dialog);
    question->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold; border:none;").arg(kTextPrimary));
    question->setWordWrap(true);
    layout->addWidget(question);

    auto *sub = new QLabel("이 작업은 즉시 실행됩니다. 계속하시겠습니까?", &dialog);
    sub->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
    sub->setWordWrap(true);
    layout->addWidget(sub);

    layout->addSpacing(10);
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setStyleSheet(QString("QPushButton { background-color:#232333; color:%1; font-size:15px; border-radius:8px; padding:14px; }").arg(kTextPrimary));
    auto *execBtn = new QPushButton("실행", &dialog);
    execBtn->setStyleSheet("QPushButton { background-color:#fbbf24; color:#241c00; font-weight:bold; font-size:15px; border-radius:8px; padding:14px; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(execBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(execBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    return dialog.exec() == QDialog::Accepted;
}

bool StatusPanel::showEvacuationConfirmDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle("대피 모드 — 최종 확인");
    dialog.setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    dialog.setMinimumWidth(460);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(14);

    auto *header = new QLabel("🚨 마지막 확인입니다", &dialog);
    header->setStyleSheet("color:#ef4444; font-size:19px; font-weight:bold; border:none;");
    layout->addWidget(header);

    auto *question = new QLabel("정말 전 구역 대피 모드를 발동하시겠습니까?", &dialog);
    question->setStyleSheet(QString("color:%1; font-size:17px; font-weight:bold; border:none;").arg(kTextPrimary));
    question->setWordWrap(true);
    layout->addWidget(question);

    auto *sub = new QLabel("전 구역에 즉시 영향을 미치며, 발동 후에는 되돌리기 어렵습니다.", &dialog);
    sub->setStyleSheet("color:#f87171; font-size:14px; font-weight:bold; border:none;");
    sub->setWordWrap(true);
    layout->addWidget(sub);

    layout->addSpacing(10);
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setStyleSheet(QString("QPushButton { background-color:#232333; color:%1; font-size:15px; border-radius:8px; padding:14px; }").arg(kTextPrimary));
    auto *execBtn = new QPushButton("대피 모드 발동", &dialog);
    execBtn->setStyleSheet("QPushButton { background-color:#ef4444; color:white; font-weight:bold; font-size:15px; border-radius:8px; padding:14px; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(execBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(execBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    return dialog.exec() == QDialog::Accepted;
}
