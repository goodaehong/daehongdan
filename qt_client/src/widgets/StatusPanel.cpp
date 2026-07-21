#include "StatusPanel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsDropShadowEffect>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
}

StatusPanel::StatusPanel(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(340);
    setStyleSheet("background-color:#14141f; border:1px solid #232333; border-radius:10px;");
    auto *heroLayout = new QVBoxLayout(this);
    heroLayout->setSpacing(10);

    heroTitleLabel = new QLabel(this);
    heroTitleLabel->setStyleSheet(QString("color:%1; font-size:13px; border:none;").arg(kTextSecondary));
    heroTitleLabel->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(heroTitleLabel);

    heroCircle = new QLabel(this);
    heroCircle->setFixedSize(90, 90);
    auto *glow = new QGraphicsDropShadowEffect;
    glow->setBlurRadius(40);
    glow->setOffset(0, 0);
    glow->setColor(QColor("#34d399"));
    heroCircle->setGraphicsEffect(glow);

    auto *circleRow = new QHBoxLayout;
    circleRow->addStretch();
    circleRow->addWidget(heroCircle);
    circleRow->addStretch();
    heroLayout->addLayout(circleRow);

    heroStateLabel = new QLabel(this);
    heroStateLabel->setAlignment(Qt::AlignCenter);
    heroStateLabel->setStyleSheet("border:none;");
    heroLayout->addWidget(heroStateLabel);
    heroLayout->addSpacing(8);

    auto addStatRow = [&](const QString &name, QLabel **valueOut) {
        auto *row = new QHBoxLayout;
        auto *nameLabel = new QLabel(name, this);
        nameLabel->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
        auto *valueLabel = new QLabel(this);
        valueLabel->setStyleSheet(QString("color:%1; font-weight:bold; border:none;").arg(kTextPrimary));
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
    auto *demoLabel = new QLabel("DEMO - 상태 시뮬레이션", this);
    demoLabel->setStyleSheet(QString("color:%1; font-size:11px; border:none;").arg(kTextSecondary));
    heroLayout->addWidget(demoLabel);

    auto *demoRow = new QHBoxLayout;
    const QStringList demoNames = { "안전", "경고", "위험" };
    for (int i = 0; i < 3; ++i) {
        auto *btn = new QPushButton(demoNames[i], this);
        btn->setCheckable(true);
        const QString c = colorForState(ZoneState(i));
        btn->setStyleSheet(QString(
            "QPushButton { color:%1; background:transparent; border:1px solid %1; border-radius:6px; padding:4px; }"
            "QPushButton:checked { background-color:%1; color:#0a0a12; font-weight:bold; }").arg(c));
        connect(btn, &QPushButton::clicked, this, [this, i]() { emit demoStateRequested(ZoneState(i)); });
        demoRow->addWidget(btn);
        demoStateButtons.append(btn);
    }
    heroLayout->addLayout(demoRow);
    heroLayout->addStretch();
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
    heroStateLabel->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold; border:none;").arg(color));

    tempValueLabel->setText(QString::number(zone.temp, 'f', 1) + " ℃");
    humidityValueLabel->setText(QString::number(zone.humidity, 'f', 0) + " %");

    if (zone.hasLiveSensorData) {
        gasValueLabel->setText(QString::number(zone.gasPpm, 'f', 0) + " ppm");
        smokeValueLabel->setText(zone.smokePpm > 20 ? "감지됨" : "미검지");
    } else {
        // DEMO 시뮬레이션(실센서 없는 구역): 상태에 따른 가짜 값
        const double gasPpm = zone.state == ZoneState::Safe ? 3 : (zone.state == ZoneState::Warning ? 8 : 15);
        gasValueLabel->setText(QString::number(gasPpm, 'f', 0) + " ppm");
        smokeValueLabel->setText(zone.state == ZoneState::Danger ? "감지됨" : "미검지");
    }

    for (int i = 0; i < demoStateButtons.size(); ++i)
        demoStateButtons[i]->setChecked(i == int(zone.state));
}

void StatusPanel::setCameraStatus(const QString &text, const QString &color)
{
    cameraStatusValueLabel->setText(text);
    cameraStatusValueLabel->setStyleSheet(QString("color:%1; font-weight:bold; border:none;").arg(color));
}
