#include "StatusPanel.h"
#include "../core/ZoneTypes.h"

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
#include <QRadioButton>
#include <QCheckBox>
#include <QLineEdit>

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
    // 회색은 "정보 없음"으로 읽혀서, 실제로 정상 동작 중임을 보여주려면 초록이 맞다
    // (액추에이터 카드의 "● 연결됨"과 같은 이유).
    return { "● 안정", kSafeColor };
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
    // 기본 스크롤바가 시스템 밝은 회색으로 떠서 다크 테마와 안 어울려 앱 accent 컬러로 맞춤.
    scrollArea->setStyleSheet(
        "QScrollArea { background:transparent; border:none; }"
        "QScrollBar:vertical { background:#14141f; width:10px; margin:0; border-radius:5px; }"
        "QScrollBar::handle:vertical { background:#3a3550; min-height:24px; border-radius:5px; }"
        "QScrollBar::handle:vertical:hover { background:#8b7cf6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; border:none; background:none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }");
    outerLayout->addWidget(scrollArea);

    auto *contentWidget = new QWidget(scrollArea);
    contentWidget->setStyleSheet("background:transparent;");
    scrollArea->setWidget(contentWidget);

    auto *heroLayout = new QVBoxLayout(contentWidget);
    heroLayout->setContentsMargins(16, 12, 16, 12);
    heroLayout->setSpacing(9);

    heroTitleLabel = new QLabel(contentWidget);
    heroTitleLabel->setStyleSheet(QString("color:%1; font-size:14px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(kTextSecondary));
    heroTitleLabel->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroTitleLabel);

    heroCircle = new QLabel(contentWidget);
    heroCircle->setFixedSize(86, 86);
    heroGlow = new QGraphicsDropShadowEffect;
    heroGlow->setBlurRadius(36);
    heroGlow->setOffset(0, 0);
    heroGlow->setColor(QColor("#34d399"));
    heroCircle->setGraphicsEffect(heroGlow);

    auto *circleRow = new QHBoxLayout;
    circleRow->addStretch();
    circleRow->addWidget(heroCircle);
    circleRow->addStretch();
    heroLayout->addLayout(circleRow);

    heroStateLabel = new QLabel(contentWidget);
    heroStateLabel->setAlignment(Qt::AlignCenter);
    heroStateLabel->setStyleSheet("border:none;");
    heroLayout->addWidget(heroStateLabel);

    // 원인 문구와 경과 시간을 같은 줄에 둔다 — 세로로 떨어져 있으면 "화재 감지 확정"과
    // "02:12 경과"가 서로 이어진 정보로 안 읽혀서 알아보기 어렵다는 피드백 반영.
    auto *causeRow = new QHBoxLayout;
    causeRow->setSpacing(6);
    causeRow->addStretch();
    heroCauseLabel = new QLabel(contentWidget);
    heroCauseLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    causeRow->addWidget(heroCauseLabel);
    heroElapsedLabel = new QLabel(contentWidget);
    heroElapsedLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    causeRow->addWidget(heroElapsedLabel);
    causeRow->addStretch();
    heroLayout->addLayout(causeRow);

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
        header->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; letter-spacing:1px; border:none;").arg(kTextSecondary));
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
        valueLabel->setStyleSheet(QString("color:%1; font-size:17px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(kTextPrimary));
        valueLabel->setAlignment(Qt::AlignRight);
        topRow->addWidget(nameLabel);
        topRow->addStretch();
        topRow->addWidget(valueLabel);
        cardLayout->addLayout(topRow);

        auto *gauge = new GaugeBar(cardWidget);
        cardLayout->addWidget(gauge);

        auto *bottomRow = new QHBoxLayout;
        auto *trendLabel = new QLabel(cardWidget);
        trendLabel->setStyleSheet(QString("font-size:12px; border:none; color:%1;").arg(kTextSecondary));
        auto *thresholdLabel = new QLabel(thresholdText, cardWidget);
        thresholdLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
        bottomRow->addWidget(trendLabel);
        bottomRow->addStretch();
        bottomRow->addWidget(thresholdLabel);
        cardLayout->addLayout(bottomRow);

        if (valueOut) *valueOut = valueLabel;
        if (gaugeOut) *gaugeOut = gauge;
        if (trendOut) *trendOut = trendLabel;
    };

    // 가스/불꽃/연기 세 센서 줄을 눈으로 구분하기 쉽게 얇은 구분선을 사이사이에 넣는다.
    auto addSensorDivider = [&](QVBoxLayout *cardLayout, QWidget *cardWidget) {
        auto *divider = new QFrame(cardWidget);
        divider->setFrameShape(QFrame::HLine);
        divider->setFixedHeight(1);
        divider->setStyleSheet(QString("background-color:%1; border:none;").arg(kCardBorder));
        cardLayout->addWidget(divider);
    };

    QVBoxLayout *sensorLayout = makeCard("위험 감지 센서");
    QWidget *sensorCardWidget = sensorLayout->parentWidget();

    // 액추에이터 카드의 "실시간 ● 연결됨"과 대칭 — 정상일 때도 "지금 믿을 수 있는 값인가"가
    // 항상 보이게 한다. 아래 수치 자리 오류 표시("--- 센서 오류")와는 역할이 다르다: 배지는
    // "지금 믿을 수 있나", 수치는 "이 값이 옛날 값이다".
    auto *sensorHeaderRow = new QHBoxLayout;
    auto *sensorRealtimeLabel = new QLabel("실시간", sensorCardWidget);
    sensorRealtimeLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    sensorLinkBadge = new QLabel(sensorCardWidget);
    sensorLinkBadge->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    sensorLinkBadge->setText("🟢 연결됨");
    sensorHeaderRow->addWidget(sensorRealtimeLabel);
    sensorHeaderRow->addStretch();
    sensorHeaderRow->addWidget(sensorLinkBadge);
    sensorLayout->addLayout(sensorHeaderRow);

    addGaugeRow(sensorLayout, sensorCardWidget, "가스 농도", "임계 2000ppm", &gasValueLabel, &gasGaugeBar, &gasTrendLabel);
    addSensorDivider(sensorLayout, sensorCardWidget);
    addGaugeRow(sensorLayout, sensorCardWidget, "불꽃 센서", "임계 1.0V", &flameValueLabel, &flameGaugeBar, &flameTrendLabel);
    addSensorDivider(sensorLayout, sensorCardWidget);

    auto *smokeRow = new QHBoxLayout;
    auto *smokeName = new QLabel("연기 농도", sensorCardWidget);
    smokeName->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
    smokeValueLabel = new QLabel(sensorCardWidget);
    smokeRow->addWidget(smokeName);
    smokeRow->addStretch();
    smokeRow->addWidget(smokeValueLabel);
    sensorLayout->addLayout(smokeRow);

    smokeGaugeBar = new GaugeBar(sensorCardWidget);
    sensorLayout->addWidget(smokeGaugeBar);

    // 가스/불꽃과 똑같이 하단 줄 오른쪽에 임계값을 보여준다 — 이전엔 여기 없어서 위험 판단 기준을
    // 알기 어려웠다. 왼쪽엔 기존 "최근 N회 판정" 이력을 그대로 유지.
    auto *smokeBottomRow = new QHBoxLayout;
    smokeHistoryLabel = new QLabel(sensorCardWidget);
    smokeHistoryLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    auto *smokeThresholdLabel = new QLabel("임계 150ppm", sensorCardWidget);
    smokeThresholdLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    smokeBottomRow->addWidget(smokeHistoryLabel);
    smokeBottomRow->addStretch();
    smokeBottomRow->addWidget(smokeThresholdLabel);
    sensorLayout->addLayout(smokeBottomRow);

    QVBoxLayout *envLayout = makeCard("환경");
    QWidget *envCardWidget = envLayout->parentWidget();

    auto *envHeaderRow = new QHBoxLayout;
    auto *envRealtimeLabel = new QLabel("실시간", envCardWidget);
    envRealtimeLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    envLinkBadge = new QLabel(envCardWidget);
    envLinkBadge->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    envLinkBadge->setText("🟢 연결됨");
    envHeaderRow->addWidget(envRealtimeLabel);
    envHeaderRow->addStretch();
    envHeaderRow->addWidget(envLinkBadge);
    envLayout->addLayout(envHeaderRow);

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
        dot->setStyleSheet("color:#6b7280; font-size:13px; border:none;");
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
    realtimeLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    actuatorLinkLabel = new QLabel(actuatorCardWidget);
    actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    actuatorLinkLabel->setText("● 확인 중");

    actuatorLinkInfoIcon = new QLabel("ⓘ", actuatorCardWidget);
    actuatorLinkInfoIcon->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
    actuatorLinkInfoIcon->setToolTip("아직 서버로부터 연결 상태를 받지 못했습니다.");

    actuatorHeaderRow->addWidget(realtimeLabel);
    actuatorHeaderRow->addStretch();
    actuatorHeaderRow->addWidget(actuatorLinkLabel);
    actuatorHeaderRow->addWidget(actuatorLinkInfoIcon);
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
            "background-color:transparent; color:%1; font-size:11px; border-radius:8px; padding:2px 6px; border:1px solid %1;")
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

    // 대피 안내 음성(스피커)은 STM 액추에이터가 아니라 서버가 트는 WAV라 fan/valve/siren과
    // 달리 "현재 상태"를 서버가 보내주지 않는다 — 그래서 addControlRow 대신 버튼 하나만 둔다.
    // 안전에 영향 없는 조작(경보 자체를 끄는 게 아니라 지금 시끄러운 소리만 끔)이라
    // 다른 제어처럼 확인 팝업 없이 바로 실행한다. 다음 위험 진입/재실행 때 서버가 다시 튼다.
    // 문구는 "음성 정지"(대홍님 제안) — action 값은 그대로 "mute"지만 실제로는 지금 재생 중인
    // 걸 멈추는 것뿐이라 "끄기/음소거"라고 하면 관리자가 다시 안 나올 거라 오해할 수 있다.
    auto *speakerLabel = new QLabel("대피 안내 음성", controlCardWidget);
    speakerLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    controlLayout->addWidget(speakerLabel);
    auto *speakerMuteBtn = new QPushButton("🔇 음성 정지", controlCardWidget);
    speakerMuteBtn->setCursor(Qt::PointingHandCursor);
    speakerMuteBtn->setFixedHeight(30);
    speakerMuteBtn->setStyleSheet(
        "QPushButton { background-color:#232333; color:#f5f5fa; border:1px solid #3a3550; "
        "border-radius:6px; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }"
        "QPushButton:hover { background-color:#2d2d40; border:1px solid #8b7cf6; }");
    connect(speakerMuteBtn, &QPushButton::clicked, this, [this]() {
        emit controlActionRequested("speaker", "mute", "대피 안내 음성 끄기");
    });
    controlLayout->addWidget(speakerMuteBtn);

    // 위험 모드 전환/해제는 교체가 아니라 2개 병존 — 상태에 따라 활성/비활성만 바뀐다 (emergency-mode #6).
    // 대홍님 요청: 해제 버튼을 숨기지 않고 항상 보이게 해서 "지금은 못 누른다"를 명확히 한다.
    // 빨간 배경 버튼에 🚨 이모지를 얹으면 이모지 자체의 빨강 톤이 배경과 겹쳐 잘 안 보였다 —
    // 굵은 흰 글씨만으로도 대비는 충분해서 아이콘 없이 문구만 둔다(다이얼로그 헤더는 어두운 배경
    // 위라 문제없어서 그대로 유지).
    emergencyTriggerButton = new QPushButton("위험 모드 전환", controlCardWidget);
    emergencyTriggerButton->setCursor(Qt::PointingHandCursor);
    emergencyTriggerButton->setFixedHeight(36);
    connect(emergencyTriggerButton, &QPushButton::clicked, this, [this]() {
        if (lastKnownZoneState == ZoneState::Safe) {
            // 정상: 서버가 원인을 모르므로 관리자가 직접 선택 (emergency-mode #8)
            QString cause;
            if (!showEmergencyCauseDialog(cause))
                return;
            // 신규 전환은 전 공장에 영향을 주고 되돌리기 어려운 조작이라 2단계로 확인한다.
            if (showEvacuationConfirmDialog())
                emit emergencyTriggerRequested(cause);
        } else if (lastKnownZoneState == ZoneState::Warning) {
            // 경고: 서버가 이미 원인을 알고 있으므로 재질의 없이 확인만 (emergency-mode #9)
            const QString causePhrase = causeText(lastKnownCause);
            if (showConfirmDialog(QString("위험 모드 전환 (원인: %1)").arg(causePhrase)))
                emit emergencyTriggerRequested(lastKnownCause);
        } else {
            // 위험·대응실패: 같은 원인으로 대응 재실행
            if (showConfirmDialog("대응 재실행"))
                emit emergencyTriggerRequested(lastKnownCause);
        }
    });

    emergencyClearButton = new QPushButton("위험 모드 해제", controlCardWidget);
    emergencyClearButton->setCursor(Qt::PointingHandCursor);
    emergencyClearButton->setFixedHeight(36);
    connect(emergencyClearButton, &QPushButton::clicked, this, [this]() {
        QString admin;
        QStringList checklist;
        if (showEmergencyClearDialog(admin, checklist))
            emit emergencyClearRequested(admin, checklist);
    });

    // 위험·대응실패 상태에서 전환 버튼을 두 톤으로 번갈아 칠하는 타이머. 매 tick마다 스타일시트를
    // 새로 세팅하므로 :hover 규칙도 그대로 살아있다(호버 반응 요청 반영).
    emergencyBlinkTimer = new QTimer(this);
    connect(emergencyBlinkTimer, &QTimer::timeout, this, [this]() {
        emergencyBlinkOn = !emergencyBlinkOn;
        applyRetryButtonBlinkStyle();
    });

    controlLayout->addSpacing(2);
    controlLayout->addWidget(emergencyTriggerButton);
    controlLayout->addWidget(emergencyClearButton);
    updateEmergencyButtons(ZoneState::Safe, true); // 서버 응답 오기 전 초기 표시

    setActuatorStatus(-1, -1, -1, "", "", "", ""); // 서버 응답 오기 전 초기 표시

    heroLayout->addStretch();
}

QString StatusPanel::pillStyle(const QString &color, bool filled) const
{
    if (filled)
        return QString("background-color:%1; color:#0a0a12; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; "
                        "border-radius:9px; padding:3px 10px; border:none;").arg(color);
    return QString("background-color:transparent; color:%1; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; "
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
    // 원인 문구 옆에 이어 붙는 자리라 " · "로 구분해준다(세로 배치였을 땐 줄바꿈으로 구분됐음).
    heroElapsedLabel->setText(QString("· %1:%2 경과")
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
    refreshChannelColor(index);
    refreshCameraHeader();
}

void StatusPanel::setCameraVisionStatus(bool ch1, bool ch2, bool ch3, bool ch4)
{
    const bool values[4] = { ch1, ch2, ch3, ch4 };
    for (int i = 0; i < 4; ++i) {
        channelVisionOk[i] = values[i];
        refreshChannelColor(i);
    }
}

// Qt 자체 영상 수신(channelConnected)과 서버 감지 생존(channelVisionOk) 조합 4색 (emergency-mode #14):
//   정상(둘 다 O) / 🟠 화면은 나오는데 서버가 못 잡음(최위험, 불 나도 자동 대응 안 됨) /
//   회색 화면만 안 나옴(감지는 정상) / 위험(둘 다 X, 카메라·MediaMTX 자체 문제)
void StatusPanel::refreshChannelColor(int index)
{
    if (index < 0 || index >= 4)
        return;
    const bool connected = channelConnected[index];
    const bool visionOk = channelVisionOk[index];
    const QString dotColor = (connected && visionOk)   ? kSafeColor
                            : (connected && !visionOk)  ? kWarnColor
                            : (!connected && visionOk)  ? kTextSecondary
                                                         : kDangerColor;
    channelDotLabels[index]->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(dotColor));
    channelFrames[index]->setStyleSheet(QString("QFrame { border:1px solid %1; border-radius:6px; }").arg(dotColor));
    channelFrames[index]->setToolTip(
        (connected && visionOk)  ? "정상" :
        (connected && !visionOk) ? "⚠ 화면은 보이지만 서버 자동 감지가 안 되고 있습니다 (최우선 확인 필요)" :
        (!connected && visionOk) ? "화면만 안 나옴 (서버 자동 감지는 정상)" :
                                    "카메라/스트리밍 연결 끊김");
}

void StatusPanel::updateZone(const Zone &zone)
{
    updateEmergencyButtons(zone.state, zone.responseOk);   // emergency-mode #6~7

    const QString color = colorForState(zone.state);
    // 상단 배지/데모 버튼 등 다른 곳은 colorForState()의 danger 톤(#f87171, 옅은 핑크빛)을 그대로
    // 쓰지만, 종합상태 구슬/문구는 위험을 더 강하게 인지시키려고 여기서만 진한 빨강으로 덮어쓴다.
    const QString heroColor = (zone.state == ZoneState::Danger) ? "#ef4444" : color;

    heroTitleLabel->setText(zone.name + " 종합상태");
    heroCircle->setStyleSheet(QString(
        "background-color: qradialgradient(cx:0.5, cy:0.4, radius:0.6, fx:0.5, fy:0.4, stop:0 white, stop:0.15 %1, stop:1 %1);"
        "border-radius:%2px;")
        .arg(heroColor).arg(heroCircle->width() / 2));
    heroGlow->setColor(QColor(heroColor));
    heroStateLabel->setText(textForState(zone.state));
    heroStateLabel->setStyleSheet(QString("color:%1; font-size:21px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(heroColor));

    const QString causePhrase = causeText(zone.cause);
    heroCauseLabel->setText(causePhrase.isEmpty() ? "이상 없음" : causePhrase);
    heroCauseLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;")
        .arg(causePhrase.isEmpty() ? kTextSecondary : heroColor));

    stateEnteredAt = zone.stateEnteredAt;
    updateElapsedLabel();

    // "자동(평상시)" vs "자동(위험)" 구분 기준이 바뀔 수 있으니 구역 상태가 갱신될 때마다 다시 그린다.
    lastKnownZoneState = zone.state;
    lastKnownCause = zone.cause;
    lastClearSensor = zone.clearSensor;
    lastClearVision = zone.clearVision;
    lastClearActuator = zone.clearActuator;
    updateModeLabel(fanModeLabel, lastFanSrc);
    updateModeLabel(valveModeLabel, lastValveSrc);
    updateModeLabel(sirenModeLabel, lastSirenSrc);

    // 배경 알약이 아니라 글자 색만 눈에 띄게 칠한다.
    // dhtOk=false는 "이번 틱에 DHT22를 못 읽어 직전 값을 재사용 중"이라는 뜻 — DHT22 특성상 흔한 일이라
    // 값을 지우진 않고(마지막 정상값이라 여전히 유효) 색만 바꾼다 (emergency-mode #13).
    // 인라인 ⚠ 아이콘은 빼고 카드 헤더의 envLinkBadge 하나로만 알린다(신호가 두 군데로 갈라지면
    // 오히려 더 헷갈림).
    const bool dhtStale = zone.hasLiveSensorData && !zone.dhtOk;
    tempValueLabel->setText(QString("%1℃").arg(QString::number(zone.temp, 'f', 1)));
    tempValueLabel->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(dhtStale ? kWarnColor : "#60a5fa"));
    tempValueLabel->setToolTip(dhtStale ? "온습도 센서 읽기 실패 — 마지막 정상값 표시 중" : "");
    humidityValueLabel->setText(QString("%1%").arg(QString::number(zone.humidity, 'f', 1)));
    humidityValueLabel->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(dhtStale ? kWarnColor : "#22d3ee"));
    humidityValueLabel->setToolTip(dhtStale ? "온습도 센서 읽기 실패 — 마지막 정상값 표시 중" : "");

    if (dhtStale) {
        envLinkBadge->setText("🟡 온습도 불안정");
        envLinkBadge->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kWarnColor));
    } else {
        envLinkBadge->setText("🟢 연결됨");
        envLinkBadge->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kSafeColor));
    }

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

    // sensorOk=false는 10초 이상 ADS1115(가스/연기/불꽃)를 못 읽었다는 뜻 — "안전"이 아니라 "판정 근거 없음"
    // 이므로 수치를 그대로 보여주면 안 된다 (emergency-mode #13). dhtOk와 달리 흔한 일이 아니라 바로 표시.
    const bool sensorStale = zone.hasLiveSensorData && !zone.sensorOk;

    // 이 카드는 가스/불꽃/연기(ADS1115) 전용이라 sensorOk만 본다 — 온습도(DHT22)는 별개
    // 센서라 위의 envLinkBadge("환경" 카드)가 따로 담당한다.
    if (sensorStale) {
        sensorLinkBadge->setText("🔴 센서 오류");
        sensorLinkBadge->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kDangerColor));
    } else {
        sensorLinkBadge->setText("🟢 연결됨");
        sensorLinkBadge->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kSafeColor));
    }

    gasValueLabel->setText(sensorStale ? "--- (센서 오류)" : QString::number(gasVal, 'f', 2) + " ppm");
    const QColor gasColor = sensorStale ? QColor(kWarnColor)
                             : gasVal >= kGasDanger ? QColor(kDangerColor)
                             : gasVal >= kGasWarn ? QColor(kWarnColor) : QColor(kSafeColor);
    gasValueLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(sensorStale ? kWarnColor : kTextPrimary));
    gasGaugeBar->setRatio(sensorStale ? 0 : gasVal / kGasScale, gasColor);
    const Trend gasTrend = trendFor(zone.gasHistory);
    gasTrendLabel->setText(gasTrend.text);
    gasTrendLabel->setStyleSheet(QString("font-size:12px; border:none; color:%1;").arg(gasTrend.color));

    flameValueLabel->setText(sensorStale ? "--- (센서 오류)" : QString::number(flameVal, 'f', 2) + " V");
    flameValueLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;")
        .arg(sensorStale ? kWarnColor : flameVal > kFlameThreshold ? kDangerColor : kTextPrimary));
    const QColor flameColor = sensorStale ? QColor(kWarnColor) : flameVal > kFlameThreshold ? QColor(kDangerColor) : QColor(kSafeColor);
    flameGaugeBar->setRatio(sensorStale ? 0 : flameVal / kFlameScale, flameColor);
    const Trend flameTrend = trendFor(zone.flameHistory);
    flameTrendLabel->setText(flameTrend.text);
    flameTrendLabel->setStyleSheet(QString("font-size:12px; border:none; color:%1;").arg(flameTrend.color));

    // 그래프 화면처럼 감지 여부 배지 대신 실제 ppm 수치를 그대로 보여준다.
    smokeValueLabel->setText(sensorStale ? "--- (센서 오류)" : QString::number(smokeVal, 'f', 2) + " ppm");
    smokeValueLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;")
        .arg(sensorStale ? kWarnColor : smokeDetected ? kDangerColor : kTextPrimary));
    smokeGaugeBar->setRatio(sensorStale ? 0 : smokeVal / kSmokeScale, sensorStale ? QColor(kWarnColor) : smokeDetected ? QColor(kDangerColor) : QColor(kSafeColor));
    int smokeDetectedCount = 0;
    for (bool b : zone.smokeDetectHistory)
        if (b) ++smokeDetectedCount;
    // gas/flame처럼 추세(●안정/▲상승/▼하강)도 앞에 붙인다 — 이전엔 이 자리에 판정 횟수만 있어서
    // 가스·불꽃 줄에만 "●안정" 표시가 있는 것처럼 보였다.
    const Trend smokeTrend = trendFor(zone.smokeHistory);
    const QString smokeHistoryText = zone.smokeDetectHistory.isEmpty() ? QString()
        : QString("최근 %1회 판정 · 감지 %2회").arg(zone.smokeDetectHistory.size()).arg(smokeDetectedCount);
    smokeHistoryLabel->setText(smokeTrend.text.isEmpty() ? smokeHistoryText
        : QString("%1 · %2").arg(smokeTrend.text, smokeHistoryText));
    smokeHistoryLabel->setStyleSheet(QString("font-size:12px; border:none; color:%1;").arg(smokeTrend.color));
}

void StatusPanel::setActuatorStatus(int fan, int valve, int siren, const QString &link,
                                     const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc,
                                     int targetFan, int targetValve, int targetSiren, const QString &linkReason)
{
    // "⟳ 대응 재실행" 버튼 문구에도 재사용 — 다음 updateZone()의 updateEmergencyButtons() 호출 때
    // (매초 오는 sensor 메시지 기준) 반영된다. sensor보다 훨씬 자주 오는 값도 아니라 즉시 갱신은 생략.
    lastLinkReason = linkReason;

    if (link == "ok") {
        actuatorLinkLabel->setText("● 연결됨");
        actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kSafeColor));
        actuatorLinkInfoIcon->setToolTip("STM 보드(환기팬·밸브·사이렌)와 정상적으로 통신 중입니다.");
    } else if (link == "down") {
        actuatorLinkLabel->setText("● 연결 끊김");
        actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kDangerColor));
        // linkReason이 오면(emergency-mode #16) 실제 사유를 그대로 보여준다. 없는 구버전 서버는
        // 기존처럼 원인을 지어내지 않고 일반적으로 확인해볼 것들만 안내한다.
        actuatorLinkInfoIcon->setToolTip(linkReason.isEmpty()
            ? "STM 보드(환기팬·밸브·사이렌)와 통신이 끊겼습니다.\n"
              "서버가 끊김 여부만 알려줄 뿐 구체적인 원인은 전달하지 않아, 아래 항목을 직접 확인해야 합니다.\n"
              "· UART 케이블이 제대로 꽂혀 있는지\n"
              "· STM 보드에 전원이 들어와 있는지\n"
              "· 서버(server_main)가 정상적으로 실행 중인지"
            : QString("STM 보드(환기팬·밸브·사이렌)와 통신이 끊겼습니다.\n사유: %1").arg(linkReason));
    } else {
        actuatorLinkLabel->setText("● 확인 중");
        actuatorLinkLabel->setStyleSheet(QString("color:%1; font-size:12px; border:none;").arg(kTextSecondary));
        actuatorLinkInfoIcon->setToolTip("아직 서버로부터 연결 상태를 한 번도 받지 못했습니다.");
    }

    lastFanSrc = fanSrc;
    lastValveSrc = valveSrc;
    lastSirenSrc = sirenSrc;
    updateModeLabel(fanModeLabel, lastFanSrc);
    updateModeLabel(valveModeLabel, lastValveSrc);
    updateModeLabel(sirenModeLabel, lastSirenSrc);

    // 목표(target)와 실제값이 다르면 명령이 STM에 반영 안 된 것 — 단, 관리자가 일부러 수동 조작한
    // 장치는 목표와 달라도 정상이므로 비교에서 뺀다 (emergency-mode #15, server의 responseApplied와 동일 기준).
    static const QStringList kFanLabels = { "OFF", "약", "중", "강" };
    // 세기별로 색을 다르게(약=파랑 → 중=노랑 → 강=빨강), OFF만 회색 아웃라인.
    static const QStringList kFanColors = { kTextSecondary, "#60a5fa", kWarnColor, kDangerColor };
    // 미반영(mismatch) 표시는 예전엔 kWarnColor(노랑) 배경에 노란 ⚠ 이모지를 얹어서 노랑-on-노랑으로
    // 거의 안 보였다(밸브 "개방" 등 원래도 노란 배지인 상태와 겹치면 특히 심함). 두 가지로 고친다:
    // ① 미반영 색은 평상시 상태색과 절대 안 겹치는 kDangerColor(빨강)로 분리
    // ② 이모지 대신 굵은 "!"— pillStyle이 지정한 진한 글씨색을 그대로 물려받아 배경과 무관하게 잘 보임
    const bool fanMismatch = fan >= 0 && fanSrc != "manual" && fan != targetFan;
    if (fan >= 0 && fan < kFanLabels.size()) {
        fanValueLabel->setText(kFanLabels[fan] + (fanMismatch ? " !" : ""));
        fanValueLabel->setStyleSheet(pillStyle(fanMismatch ? kDangerColor : kFanColors[fan], fan != 0 || fanMismatch));
        fanValueLabel->setToolTip(fanMismatch
            ? QString("⚠ 대응 미반영 — 목표: %1 → 실제: %2").arg(kFanLabels.value(targetFan, "?"), kFanLabels[fan]) : "");
    } else {
        fanValueLabel->setText("확인 중");
        fanValueLabel->setStyleSheet(pillStyle(kTextSecondary, false));
        fanValueLabel->setToolTip("");
    }

    const bool valveMismatch = (valve == 0 || valve == 1) && valveSrc != "manual" && valve != targetValve;
    if (valve == 0 || valve == 1) {
        valveValueLabel->setText((valve == 1 ? "개방" : "잠금") + QString(valveMismatch ? " !" : ""));
        valveValueLabel->setStyleSheet(pillStyle(valveMismatch ? kDangerColor : (valve == 1 ? kWarnColor : kSafeColor), true));
        valveValueLabel->setToolTip(valveMismatch
            ? QString("⚠ 대응 미반영 — 목표: %1 → 실제: %2").arg(targetValve == 1 ? "개방" : "잠금", valve == 1 ? "개방" : "잠금") : "");
    } else {
        valveValueLabel->setText("확인 중");
        valveValueLabel->setStyleSheet(pillStyle(kTextSecondary, false));
        valveValueLabel->setToolTip("");
    }

    const bool sirenMismatch = (siren == 0 || siren == 1) && sirenSrc != "manual" && siren != targetSiren;
    if (siren == 0 || siren == 1) {
        sirenValueLabel->setText((siren == 1 ? "ON" : "OFF") + QString(sirenMismatch ? " !" : ""));
        sirenValueLabel->setStyleSheet(pillStyle(sirenMismatch ? kDangerColor : (siren == 1 ? kDangerColor : kTextSecondary), siren == 1 || sirenMismatch));
        sirenValueLabel->setToolTip(sirenMismatch
            ? QString("⚠ 대응 미반영 — 목표: %1 → 실제: %2").arg(targetSiren == 1 ? "ON" : "OFF", siren == 1 ? "ON" : "OFF") : "");
    } else {
        sirenValueLabel->setText("확인 중");
        sirenValueLabel->setStyleSheet(pillStyle(kTextSecondary, false));
        sirenValueLabel->setToolTip("");
    }

    updateControlButtonStyles(fanCtrlButtons, fan);
    updateControlButtonStyles(valveCtrlButtons, valve);
    updateControlButtonStyles(sirenCtrlButtons, siren);
}

void StatusPanel::updateModeLabel(QLabel *label, const QString &source)
{
    // 예전엔 투명 배경 + 10px 얇은 글씨라 카드 배경 위에서 거의 안 보였음 — 다른 상태 배지와
    // 동일하게 pillStyle(filled)로 채워서 대비를 확실히 준다.
    if (source == "manual") {
        label->setText("수동 개입");
        label->setStyleSheet(pillStyle(kWarnColor, true));
    } else if (source == "auto") {
        const bool emergency = lastKnownZoneState != ZoneState::Safe;
        label->setText(emergency ? "자동(위험)" : "자동(평상시)");
        label->setStyleSheet(pillStyle(emergency ? kDangerColor : kSafeColor, true));
    } else {
        label->setText("확인 중");
        label->setStyleSheet(pillStyle(kTextSecondary, false));
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

// 상태별 버튼 표(emergency-mode #6~7, 대홍님 요청으로 두 버튼 항상 표시로 변경):
// 정상/경고 = 전환 빨강 활성 · 해제 회색 비활성
// 위험·대응중 = 전환 회색 비활성 · 해제 빨강 활성
// 위험·대응실패 = 전환 주황 깜빡임("⟳ 대응 재실행 (사유)") · 해제 빨강 활성
void StatusPanel::updateEmergencyButtons(ZoneState state, bool responseOk)
{
    if (!emergencyTriggerButton || !emergencyClearButton)
        return;

    if (state == ZoneState::Danger) {
        if (responseOk) {
            emergencyBlinkTimer->stop();
            emergencyTriggerButton->setEnabled(false);
            emergencyTriggerButton->setText("위험 모드 전환");
            emergencyTriggerButton->setStyleSheet(
                "QPushButton { background-color:#3a3550; color:#8d87a0; border:none; border-radius:6px; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }");
            setClearButtonActive(true);
        } else {
            emergencyTriggerButton->setEnabled(true);
            updateRetryButtonText();
            emergencyBlinkOn = true;
            applyRetryButtonBlinkStyle();
            emergencyBlinkTimer->start(600);   // 대응 실패 = "지금 확인해야 할 것" -> 눈에 띄게 깜빡임
            setClearButtonActive(true);
        }
    } else {
        emergencyBlinkTimer->stop();
        emergencyTriggerButton->setEnabled(true);
        emergencyTriggerButton->setText("위험 모드 전환");
        emergencyTriggerButton->setStyleSheet(
            "QPushButton { background-color:#ef4444; color:white; border:none; border-radius:6px; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }"
            "QPushButton:hover { background-color:#dc2626; }");
        setClearButtonActive(false);
    }
}

void StatusPanel::setClearButtonActive(bool active)
{
    emergencyClearButton->setEnabled(active);
    emergencyClearButton->setStyleSheet(active
        ? "QPushButton { background-color:#ef4444; color:white; border:none; border-radius:6px; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }"
          "QPushButton:hover { background-color:#dc2626; }"
        : "QPushButton { background-color:#3a3550; color:#8d87a0; border:none; border-radius:6px; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }");
}

void StatusPanel::updateRetryButtonText()
{
    QString text = "⟳ 대응 재실행";
    if (!lastLinkReason.isEmpty())
        text += QString(" (%1)").arg(lastLinkReason);
    emergencyTriggerButton->setText(text);
}

void StatusPanel::applyRetryButtonBlinkStyle()
{
    const QString bg = emergencyBlinkOn ? "#f59e0b" : "#7c4a08";
    emergencyTriggerButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:#241c00; border:none; border-radius:6px; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }"
        "QPushButton:hover { background-color:#d97706; }").arg(bg));
}

void StatusPanel::updateControlButtonStyles(QVector<QPushButton *> &buttons, int activeIndex)
{
    for (int i = 0; i < buttons.size(); ++i) {
        const bool active = (i == activeIndex);
        buttons[i]->setStyleSheet(active
            ? "QPushButton { background-color:#8b7cf6; color:white; border:none; border-radius:6px; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }"
            : QString("QPushButton { background-color:#232333; color:%1; border:none; border-radius:6px; font-size:13px; }").arg(kTextSecondary));
    }
}

void StatusPanel::showCommandStatus(const QString &text, const QString &color)
{
    commandStatusLabel->setText(text);
    commandStatusLabel->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(color));
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
    header->setStyleSheet("color:#fbbf24; font-size:18px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;");
    layout->addWidget(header);

    auto *question = new QLabel(QString("정말 '%1'를 실행하시겠습니까?").arg(actionName), &dialog);
    question->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(kTextPrimary));
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
    execBtn->setStyleSheet("QPushButton { background-color:#fbbf24; color:#241c00; font-weight:bold; font-family:\"hanwhaGothic EL\"; font-size:15px; border-radius:8px; padding:14px; }");
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
    header->setStyleSheet("color:#ef4444; font-size:19px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;");
    layout->addWidget(header);

    auto *question = new QLabel("정말 전 공장 대피 모드를 발동하시겠습니까?", &dialog);
    question->setStyleSheet(QString("color:%1; font-size:17px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(kTextPrimary));
    question->setWordWrap(true);
    layout->addWidget(question);

    auto *sub = new QLabel("전 공장에 즉시 영향을 미치며, 발동 후에는 되돌리기 어렵습니다.", &dialog);
    sub->setStyleSheet("color:#f87171; font-size:14px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;");
    sub->setWordWrap(true);
    layout->addWidget(sub);

    layout->addSpacing(10);
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setStyleSheet(QString("QPushButton { background-color:#232333; color:%1; font-size:15px; border-radius:8px; padding:14px; }").arg(kTextPrimary));
    auto *execBtn = new QPushButton("대피 모드 발동", &dialog);
    execBtn->setStyleSheet("QPushButton { background-color:#ef4444; color:white; font-weight:bold; font-family:\"hanwhaGothic EL\"; font-size:15px; border-radius:8px; padding:14px; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(execBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(execBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    return dialog.exec() == QDialog::Accepted;
}


bool StatusPanel::showEmergencyCauseDialog(QString &outCause)
{
    QDialog dialog(this);
    dialog.setWindowTitle("위험 모드 전환");
    dialog.setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    dialog.setMinimumWidth(380);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(14);

    auto *header = new QLabel("🚨 위험 모드 전환", &dialog);
    header->setStyleSheet("color:#ef4444; font-size:18px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;");
    layout->addWidget(header);

    auto *question = new QLabel("원인을 선택하세요.", &dialog);
    question->setStyleSheet(QString("color:%1; font-size:15px; border:none;").arg(kTextPrimary));
    layout->addWidget(question);

    // 기본값 화재 — 오판 시 피해가 작은 쪽 (가스로 오판하면 팬이 강풍으로 돌아 화재를 키움)
    auto *fireRadio = new QRadioButton("화재", &dialog);
    auto *gasRadio = new QRadioButton("가스 누출", &dialog);
    auto *smokeRadio = new QRadioButton("연기", &dialog);
    fireRadio->setChecked(true);
    // 선택된 항목이 눈에 띄게: 텍스트는 흰색+굵게, 원 표시는 빨간 테두리+채움으로 구분.
    // 선택 안 된 항목은 어두운 회색으로 낮춰서 대비를 준다.
    const QString radioStyle = QString(
        "QRadioButton { color:%1; font-size:15px; border:none; padding:6px 0; }"
        "QRadioButton:checked { color:%2; font-weight:bold; font-family:\"hanwhaGothic EL\"; }"
        "QRadioButton::indicator { width:18px; height:18px; border-radius:9px; border:2px solid %1; background:transparent; }"
        "QRadioButton::indicator:checked { border:2px solid #ef4444; background:#ef4444; }"
    ).arg(kTextSecondary, kTextPrimary);
    for (QRadioButton *r : { fireRadio, gasRadio, smokeRadio })
        r->setStyleSheet(radioStyle);
    layout->addWidget(fireRadio);
    layout->addWidget(gasRadio);
    layout->addWidget(smokeRadio);

    layout->addSpacing(10);
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setStyleSheet(QString("QPushButton { background-color:#232333; color:%1; font-size:15px; border-radius:8px; padding:14px; }").arg(kTextPrimary));
    auto *nextBtn = new QPushButton("다음", &dialog);
    nextBtn->setStyleSheet("QPushButton { background-color:#ef4444; color:white; font-weight:bold; font-family:\"hanwhaGothic EL\"; font-size:15px; border-radius:8px; padding:14px; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(nextBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(nextBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (gasRadio->isChecked())
        outCause = "gas";
    else if (smokeRadio->isChecked())
        outCause = "smoke_confirmed";
    else
        outCause = "fire_confirmed";
    return true;
}

bool StatusPanel::showEmergencyClearDialog(QString &outAdmin, QStringList &outChecklist)
{
    // 원인별 현장 확인 문구 + 서버로 보낼 키. 관련 없는 항목까지 나열하면 형식적 체크를 유발하므로
    // 원인 카테고리(화재/가스/연기)당 2개씩만 보여준다. "대피 인원 복귀 확인"은 원인 무관 공통.
    QString field1Text, field1Key, field2Text, field2Key;
    if (lastKnownCause.contains("gas")) {
        field1Text = "가스 냄새 없음 확인";       field1Key = "gas_smell";
        field2Text = "밸브 잠금 및 누출 지점 조치"; field2Key = "valve_closed";
    } else if (lastKnownCause.contains("smoke")) {
        field1Text = "연기 발생원 확인";           field1Key = "smoke_source_confirmed";
        field2Text = "연기 발생원 제거 및 환기 완료"; field2Key = "smoke_source_removed";
    } else {
        field1Text = "화염·잔불 없음 확인";        field1Key = "flame_confirmed_clear";
        field2Text = "발화원 제거 및 소화 완료";    field2Key = "ignition_source_removed";
    }

    QDialog dialog(this);
    dialog.setWindowTitle("위험 모드 해제");
    dialog.setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    dialog.setMinimumWidth(420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(32, 28, 32, 28);
    layout->setSpacing(10);

    auto *header = new QLabel(QString("위험 모드 해제 — 원인: %1").arg(causeText(lastKnownCause)), &dialog);
    header->setStyleSheet("color:#f59e0b; font-size:17px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;");
    header->setWordWrap(true);
    layout->addWidget(header);

    auto *sysLabel = new QLabel("시스템 확인 (자동 판정)", &dialog);
    sysLabel->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(kTextSecondary));
    layout->addWidget(sysLabel);

    // 서버가 판정하는 3항목 — 그대로 보여주기만, 사용자가 못 누르게 disabled.
    auto *sysSensorBox = new QCheckBox("센서 수치 정상 복귀", &dialog);
    auto *sysVisionBox = new QCheckBox("영상 감지 정상", &dialog);
    auto *sysActuatorBox = new QCheckBox("액추에이터 정상", &dialog);
    sysSensorBox->setChecked(lastClearSensor);
    sysVisionBox->setChecked(lastClearVision);
    sysActuatorBox->setChecked(lastClearActuator);
    for (QCheckBox *c : { sysSensorBox, sysVisionBox, sysActuatorBox }) {
        c->setEnabled(false);
        c->setStyleSheet(QString("color:%1; font-size:14px; padding:2px 0;").arg(kTextSecondary));
        layout->addWidget(c);
    }

    layout->addSpacing(6);
    auto *fieldLabel = new QLabel("현장 확인 (직접 확인 후 체크)", &dialog);
    fieldLabel->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\"; border:none;").arg(kTextSecondary));
    layout->addWidget(fieldLabel);

    auto *field1Box = new QCheckBox(field1Text, &dialog);
    auto *field2Box = new QCheckBox(field2Text, &dialog);
    auto *personnelBox = new QCheckBox("대피 인원 복귀 확인", &dialog);
    for (QCheckBox *c : { field1Box, field2Box, personnelBox }) {
        c->setStyleSheet(QString("color:%1; font-size:14px; padding:2px 0;").arg(kTextPrimary));
        layout->addWidget(c);
    }

    layout->addSpacing(6);
    auto *nameLabel = new QLabel("확인자", &dialog);
    nameLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    layout->addWidget(nameLabel);
    auto *nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText("이름 입력");
    nameEdit->setStyleSheet(QString("QLineEdit { background-color:#14141f; color:%1; border:1px solid %2; border-radius:6px; padding:8px; font-size:14px; }").arg(kTextPrimary, kCardBorder));
    layout->addWidget(nameEdit);

    layout->addSpacing(10);
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setStyleSheet(QString("QPushButton { background-color:#232333; color:%1; font-size:15px; border-radius:8px; padding:14px; }").arg(kTextPrimary));
    auto *confirmBtn = new QPushButton("해제 확정", &dialog);
    confirmBtn->setEnabled(false);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(confirmBtn);
    layout->addLayout(btnRow);

    // 시스템 확인 3개(고정값) + 현장 확인 2개 + 대피 확인 + 이름 입력, 전부 충족해야 활성화.
    const bool systemChecksPass = lastClearSensor && lastClearVision && lastClearActuator;
    auto refreshConfirmEnabled = [=]() {
        const bool ready = systemChecksPass && field1Box->isChecked() && field2Box->isChecked()
                            && personnelBox->isChecked() && !nameEdit->text().trimmed().isEmpty();
        confirmBtn->setEnabled(ready);
        confirmBtn->setStyleSheet(ready
            ? "QPushButton { background-color:#f59e0b; color:#241c00; font-weight:bold; font-family:\"hanwhaGothic EL\"; font-size:15px; border-radius:8px; padding:14px; }"
            : "QPushButton { background-color:#3a3550; color:#8d87a0; font-size:15px; border-radius:8px; padding:14px; }");
    };
    connect(field1Box, &QCheckBox::toggled, &dialog, refreshConfirmEnabled);
    connect(field2Box, &QCheckBox::toggled, &dialog, refreshConfirmEnabled);
    connect(personnelBox, &QCheckBox::toggled, &dialog, refreshConfirmEnabled);
    connect(nameEdit, &QLineEdit::textChanged, &dialog, refreshConfirmEnabled);
    refreshConfirmEnabled();

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(confirmBtn, &QPushButton::clicked, &dialog, &QDialog::accept);

    if (dialog.exec() != QDialog::Accepted)
        return false;

    outAdmin = nameEdit->text().trimmed();
    outChecklist = { field1Key, field2Key, "personnel_returned" };
    return true;
}