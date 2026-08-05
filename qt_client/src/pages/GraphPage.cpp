#include "GraphPage.h"
#include "../widgets/GasGraphWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";
}

GraphPage::GraphPage(QWidget *parent)
    : QWidget(parent)
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 20, 24, 20);
    rootLayout->setSpacing(12);

    rootLayout->addWidget(createControlBar());

    auto *chartsRow = new QHBoxLayout;
    chartsRow->setSpacing(16);

    auto *gasCol = new QVBoxLayout;
    gasTitleLabel = new QLabel(this);
    gasTitleLabel->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold;").arg(kTextPrimary));
    gasCol->addWidget(gasTitleLabel);
    gasCol->addSpacing(8);
    gasGraph = new GasGraphWidget(this);
    gasGraph->setLineColor(QColor("#8b7cf6"));
    gasGraph->setUnit("ppm");
    gasGraph->setThresholds(200, 2000); // MQ-9 LPG 곡선 기준: 경고(전광판 주황) 200ppm / 위험 2000ppm (server/judgement.cpp)
    gasCol->addWidget(gasGraph, 1);
    chartsRow->addLayout(gasCol);

    auto *smokeCol = new QVBoxLayout;
    smokeTitleLabel = new QLabel(this);
    smokeTitleLabel->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold;").arg(kTextPrimary));
    smokeCol->addWidget(smokeTitleLabel);
    smokeCol->addSpacing(8);
    smokeGraph = new GasGraphWidget(this);
    smokeGraph->setLineColor(QColor("#fb923c"));
    smokeGraph->setUnit("ppm");
    // MQ-2 위험 임계값 150ppm(server/judgement.cpp). 경고 단계 임계값은 코드에 명시 안 돼있어
    // 위험치의 절반(75ppm)을 잠정 사용 — 실측 기준 정해지면 조정 필요.
    smokeGraph->setThresholds(75, 150);
    smokeCol->addWidget(smokeGraph, 1);
    chartsRow->addLayout(smokeCol);

    rootLayout->addLayout(chartsRow, 1);
}

QWidget *GraphPage::createControlBar()
{
    auto *bar = new QFrame(this);
    bar->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:8px;").arg(kCardBg, kCardBorder));
    auto *outer = new QVBoxLayout(bar);
    outer->setContentsMargins(14, 10, 14, 10);
    outer->setSpacing(6);

    auto *row = new QHBoxLayout;
    row->setSpacing(6);

    static const QStringList kPeriodNames = { "10분", "1시간", "6시간", "하루" };
    const QString periodBtnStyle = QString(
        "QPushButton { color:%1; background:transparent; border:1px solid %2; border-radius:6px; padding:5px 12px; }"
        "QPushButton:checked { background-color:%3; color:white; border:1px solid %3; }")
        .arg(kTextSecondary, kCardBorder, kAccent);

    periodButtons.clear();
    for (int i = 0; i < kPeriodNames.size(); ++i) {
        auto *btn = new QPushButton(kPeriodNames[i], bar);
        btn->setCheckable(true);
        btn->setStyleSheet(periodBtnStyle);
        connect(btn, &QPushButton::clicked, this, [this, i]() { selectPeriod(i); });
        row->addWidget(btn);
        periodButtons.append(btn);
    }

    row->addSpacing(12);

    const QString navBtnStyle = QString(
        "QPushButton { color:%1; background:transparent; border:1px solid %2; border-radius:6px; padding:5px 10px; }"
        "QPushButton:hover { border:1px solid %3; color:%3; }"
        "QPushButton:disabled { color:#4a4658; border:1px solid #2a2a38; }")
        .arg(kTextSecondary, kCardBorder, kAccent);

    dateButton = new QPushButton(bar);
    dateButton->setStyleSheet(navBtnStyle);
    dateButton->setToolTip("날짜 선택 (DB 연동 후 달력 팝업 예정)");
    row->addWidget(dateButton);

    prevButton = new QPushButton("◀️ 이전", bar);
    todayButton = new QPushButton("지금", bar);
    nextButton = new QPushButton("다음 ▶️", bar);
    for (QPushButton *btn : { prevButton, todayButton, nextButton }) {
        btn->setStyleSheet(navBtnStyle);
        row->addWidget(btn);
    }
    connect(prevButton, &QPushButton::clicked, this, [this]() {
        currentDate = currentDate.addDays(-1);
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        updateNavButtons();
        // TODO: 서버 query 연동되면 currentDate 기준으로 sendQuery("sensor_log", {...}) 재요청
    });
    connect(nextButton, &QPushButton::clicked, this, [this]() {
        currentDate = currentDate.addDays(1);
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        updateNavButtons();
    });
    connect(todayButton, &QPushButton::clicked, this, [this]() {
        currentDate = QDate::currentDate();
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        updateNavButtons();
    });

    row->addStretch();
    outer->addLayout(row);

    legendLabel = new QLabel(bar);
    legendLabel->setStyleSheet(QString("color:%1; font-size:11px;").arg(kTextSecondary));
    legendLabel->setText("● AVG: 1분 평균값   ▬ MAX: 1분 내 최댓값   ⓘ");
    legendLabel->setToolTip("구간이 넓어지면 데이터가 많아 평균으로 묶어 표시합니다.\n"
                             "평균만 보면 짧은 급상승이 묻히기 때문에, 그 구간의 최댓값도 함께 표시합니다.");
    outer->addWidget(legendLabel);

    noteLabel = new QLabel(
        "※ 서버 DB 조회(query) 연동 전이라 기간/날짜 선택은 화면만 준비된 상태입니다. "
        "실제 연동되면 이 화면 그대로 데이터만 서버 조회 결과로 바뀝니다.", bar);
    noteLabel->setStyleSheet(QString("color:%1; font-size:10px;").arg(kTextSecondary));
    noteLabel->setWordWrap(true);
    outer->addWidget(noteLabel);

    currentDate = QDate::currentDate();
    dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
    selectPeriod(0);
    updateNavButtons();

    return bar;
}

void GraphPage::selectPeriod(int index)
{
    currentPeriodIndex = index;
    for (int i = 0; i < periodButtons.size(); ++i)
        periodButtons[i]->setChecked(i == index);
    // 10분/1시간은 원본 그대로(선 1개), 6시간/하루는 구간 집계(AVG+MAX 두 선)라 범례를 켠다.
    legendLabel->setVisible(index >= 2);
    // TODO: 서버 query/query_result 붙으면 여기서 sendQuery("sensor_log", {zone, from, to})로 교체.
}

void GraphPage::updateNavButtons()
{
    const bool isToday = currentDate == QDate::currentDate();
    nextButton->setEnabled(!isToday);
    todayButton->setEnabled(!isToday);
}

void GraphPage::updateZone(const Zone &zone)
{
    // 값 범위는 judgement.cpp 실측 임계값(가스 경고200/위험2000ppm, 연기 경고75/위험150ppm)에 맞춰 조정.
    const QVector<double> gasSeries =
        zone.state == ZoneState::Safe ? QVector<double>{ 30, 35, 32, 38, 34, 36 }
        : zone.state == ZoneState::Warning ? QVector<double>{ 60, 120, 180, 220, 190, 150 }
                                            : QVector<double>{ 300, 900, 1800, 2500, 2200, 1900 };
    const QVector<double> smokeSeries =
        zone.state == ZoneState::Safe ? QVector<double>{ 10, 15, 12, 18, 14, 16 }
        : zone.state == ZoneState::Warning ? QVector<double>{ 30, 55, 70, 90, 80, 60 }
                                            : QVector<double>{ 60, 110, 160, 220, 190, 150 };

    gasGraph->setData(gasSeries, { "12:00", "23:00" });
    gasTitleLabel->setText("가스 농도 추이 — " + zone.name + " (ppm)");
    smokeGraph->setData(smokeSeries, { "12:00", "23:00" });
    smokeTitleLabel->setText("연기 농도 추이 — " + zone.name + " (ppm)");
}
