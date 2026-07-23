#include "GraphPage.h"
#include "../widgets/GasGraphWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

namespace {
const QString kTextPrimary = "#f5f5fa";
}

GraphPage::GraphPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);

    auto *gasCol = new QVBoxLayout;
    gasTitleLabel = new QLabel(this);
    gasTitleLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    gasCol->addWidget(gasTitleLabel);
    gasCol->addSpacing(8);
    gasGraph = new GasGraphWidget(this);
    gasGraph->setLineColor(QColor("#8b7cf6"));
    gasGraph->setUnit("ppm");
    gasGraph->setThresholds(5, 10); // 경고 5ppm / 위험 10ppm (임시 기준, 서버 확정값 나오면 조정)
    gasCol->addWidget(gasGraph, 1);
    layout->addLayout(gasCol);

    auto *smokeCol = new QVBoxLayout;
    smokeTitleLabel = new QLabel(this);
    smokeTitleLabel->setStyleSheet(QString("color:%1; font-size:15px; font-weight:bold;").arg(kTextPrimary));
    smokeCol->addWidget(smokeTitleLabel);
    smokeCol->addSpacing(8);
    smokeGraph = new GasGraphWidget(this);
    smokeGraph->setLineColor(QColor("#fb923c"));
    smokeGraph->setUnit("%");
    smokeGraph->setThresholds(15, 50); // 경고 15% / 위험 50% (임시 기준, 서버 확정값 나오면 조정)
    smokeCol->addWidget(smokeGraph, 1);
    layout->addLayout(smokeCol);
}

void GraphPage::updateZone(const Zone &zone)
{
    const QVector<double> gasSeries =
        zone.state == ZoneState::Safe ? QVector<double>{ 2, 2.5, 3, 2.8, 3.2, 3 }
        : zone.state == ZoneState::Warning ? QVector<double>{ 3, 4, 6, 8, 7, 6 }
                                            : QVector<double>{ 3, 6, 10, 15, 13, 12 };
    const QVector<double> smokeSeries =
        zone.state == ZoneState::Safe ? QVector<double>{ 5, 8, 10, 9, 7, 6 }
        : zone.state == ZoneState::Warning ? QVector<double>{ 10, 20, 35, 45, 40, 32 }
                                            : QVector<double>{ 15, 35, 60, 85, 78, 70 };

    gasGraph->setData(gasSeries, { "12:00", "23:00" });
    gasTitleLabel->setText("가스 농도 추이 — " + zone.name + " (ppm)");
    smokeGraph->setData(smokeSeries, { "12:00", "23:00" });
    smokeTitleLabel->setText("연기 위험도 추이 — " + zone.name + " (%)");
}
