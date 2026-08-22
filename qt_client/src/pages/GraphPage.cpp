#include "GraphPage.h"
#include "../widgets/GasGraphWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>
#include <QCalendarWidget>
#include <QTextCharFormat>
#include <QTimer>

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
    chartsRow->setSpacing(10);

    auto *gasCol = new QVBoxLayout;
    gasTitleLabel = new QLabel(this);
    gasTitleLabel->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
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
    smokeTitleLabel->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
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

    // 그래프 화면에 머무는 동안 시간 축이 실시간으로 앞으로 움직이도록 주기적으로 재조회한다.
    // 예전엔 기간 버튼을 누른 그 순간에만 조회하고 끝이라, 계속 보고 있어도 그래프가 멈춰있었다.
    // 고정 30초 주기 대신 실제 시계의 "분"이 바뀌는 순간에 맞춰 갱신한다.
    liveRefreshTimer = new QTimer(this);
    liveRefreshTimer->setSingleShot(true);
    connect(liveRefreshTimer, &QTimer::timeout, this, [this]() {
        requestCurrentPeriod();
        scheduleNextMinuteTick();
    });
    scheduleNextMinuteTick();
}

void GraphPage::scheduleNextMinuteTick()
{
    const QTime now = QTime::currentTime();
    const int msUntilNextMinute = 60000 - (now.second() * 1000 + now.msec());
    liveRefreshTimer->start(msUntilNextMinute > 0 ? msUntilNextMinute : 60000);
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
        "QPushButton { color:%1; background:transparent; border:1px solid %2; border-radius:6px; padding:6px 14px; font-size:14px; }"
        "QPushButton:checked { background-color:%3; color:white; border:1px solid %3; }"
        "QPushButton:disabled { color:#4a4658; border:1px solid #2a2a38; background:transparent; }")
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
        "QPushButton { color:%1; background:transparent; border:1px solid %2; border-radius:6px; padding:6px 12px; font-size:14px; }"
        "QPushButton:hover { border:1px solid %3; color:%3; }"
        "QPushButton:disabled { color:#4a4658; border:1px solid #2a2a38; }")
        .arg(kTextSecondary, kCardBorder, kAccent);

    dateButton = new QPushButton(bar);
    dateButton->setStyleSheet(navBtnStyle);
    dateButton->setToolTip("클릭하면 달력에서 날짜를 직접 고를 수 있습니다");
    connect(dateButton, &QPushButton::clicked, this, &GraphPage::showDatePicker);
    row->addWidget(dateButton);

    prevButton = new QPushButton("◀️ 이전 날", bar);
    todayButton = new QPushButton("현재", bar);
    nextButton = new QPushButton("다음 날 ▶️", bar);
    for (QPushButton *btn : { prevButton, todayButton, nextButton }) {
        btn->setStyleSheet(navBtnStyle);
        row->addWidget(btn);
    }
    connect(prevButton, &QPushButton::clicked, this, [this]() {
        currentDate = currentDate.addDays(-1);
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        updateNavButtons();
        requestCurrentPeriod();
    });
    connect(nextButton, &QPushButton::clicked, this, [this]() {
        currentDate = currentDate.addDays(1);
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        updateNavButtons();
        requestCurrentPeriod();
    });
    connect(todayButton, &QPushButton::clicked, this, [this]() {
        currentDate = QDate::currentDate();
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        updateNavButtons();
        requestCurrentPeriod();
    });

    row->addStretch();
    outer->addLayout(row);

    legendLabel = new QLabel(bar);
    legendLabel->setStyleSheet("color:#d8d4e8; font-size:13px;");
    legendLabel->setText("● AVG: 구간 평균값   ▬ MAX: 구간 내 최댓값 (그래프 안 우측 상단에도 표시)   ⓘ");
    legendLabel->setToolTip("구간이 넓어지면 데이터가 많아 평균으로 묶어 표시합니다.\n"
                             "평균만 보면 짧은 급상승이 묻히기 때문에, 그 구간의 최댓값도 함께 표시합니다.\n"
                             "그래프 위에 마우스를 올리면 해당 시각의 값을 볼 수 있습니다.");
    outer->addWidget(legendLabel);

    noteLabel = new QLabel(bar);
    noteLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    noteLabel->setVisible(false); // 조회 실패했을 때만 showQueryFailed()가 채워서 보여줌
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
    updateNavButtons(); // "하루" + 과거 날짜 조합이면 짧은 기간 버튼을 잠가야 하니 다시 계산
    requestCurrentPeriod();
}

void GraphPage::updateNavButtons()
{
    const bool isToday = currentDate == QDate::currentDate();
    nextButton->setEnabled(!isToday);
    todayButton->setEnabled(!isToday);

    // "하루"로 과거 날짜를 보는 중엔 10분/1시간/6시간("지금 기준 최근 N")이 의미가 안 맞는다 —
    // requestCurrentPeriod()가 그 날 23:59:59부터 거슬러 올라가는 걸로 어색하게 처리하고
    // 있었으므로, 아예 못 누르게 잠근다. "하루" 버튼 자신은 항상 눌려있는 채로 둔다.
    const bool restrictShortPeriods = (currentPeriodIndex == periodButtons.size() - 1) && !isToday;
    for (int i = 0; i < periodButtons.size() - 1; ++i)
        periodButtons[i]->setEnabled(!restrictShortPeriods);
}

void GraphPage::showDatePicker()
{
    // Qt::Popup: 테두리 없는 드롭다운처럼 동작 — 바깥을 클릭하거나 포커스를 잃으면 자동으로 닫힘.
    // 콤보박스 팝업 느낌으로 날짜 버튼 바로 아래에 띄운다(화면 가운데 모달 대신).
    auto *popup = new QFrame(this, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setStyleSheet("background-color:#14141f; border:1px solid #333344; border-radius:8px;");

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *calendar = new QCalendarWidget(popup);
    calendar->setGridVisible(true);
    // 기본값은 왼쪽에 ISO 주차 번호(31~36 등)를 같이 보여주는데, 날짜(1~31)와 헷갈려서 숨긴다.
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setMaximumDate(QDate::currentDate()); // 미래 날짜는 데이터가 없으니 선택 불가
    calendar->setSelectedDate(currentDate);
    calendar->setStyleSheet(
        "QCalendarWidget { background-color:#14141f; color:#f5f5fa; }"
        "QCalendarWidget QToolButton { color:#f5f5fa; background-color:transparent; font-size:14px; icon-size:20px; padding:6px; }"
        "QCalendarWidget QToolButton:hover { background-color:#232333; border-radius:6px; }"
        "QCalendarWidget QMenu { background-color:#1a1a26; color:#f5f5fa; }"
        "QCalendarWidget QSpinBox { background-color:#1a1a26; color:#f5f5fa; }"
        "QCalendarWidget QAbstractItemView:enabled { background-color:#14141f; color:#f5f5fa; }"
        // 클릭해서 선택한 칸에 직접 테두리를 그린다 (배경색만으로는 잘 안 띄었음).
        "QCalendarWidget QAbstractItemView::item:selected { border:2px solid #a78bfa; "
        "background-color:#8b7cf6; color:white; border-radius:4px; }"
        "QCalendarWidget QAbstractItemView:disabled { color:#4a4658; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background-color:#1a1a26; }");

    // 오늘 날짜를 굵게 + 강조색으로 표시(선택은 안 돼도 눈에 띄게).
    QTextCharFormat todayFormat;
    todayFormat.setFontWeight(QFont::Bold);
    todayFormat.setForeground(QColor(kAccent));
    calendar->setDateTextFormat(QDate::currentDate(), todayFormat);

    layout->addWidget(calendar);

    auto pickDate = [this, popup](const QDate &date) {
        currentDate = date;
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        updateNavButtons();
        requestCurrentPeriod();
        // 클릭한 칸이 보라색으로 바뀌는 게 눈에 보일 시간을 살짝 준 뒤 닫는다.
        // (바로 닫아버리면 선택 표시가 뜨는 걸 볼 틈이 없었다.)
        QTimer::singleShot(150, popup, [popup]() { popup->close(); });
    };
    connect(calendar, &QCalendarWidget::clicked, popup, pickDate);
    connect(calendar, &QCalendarWidget::activated, popup, pickDate);

    popup->move(dateButton->mapToGlobal(QPoint(0, dateButton->height() + 4)));
    popup->show();
}

void GraphPage::requestCurrentPeriod()
{
    if (currentZoneId.isEmpty())
        return; // 아직 구역 정보를 한 번도 못 받음(초기 생성 시점 등)

    // 기간 버튼 4개 = 10분/1시간/6시간/하루 (명세서 3-2 기간 버튼 표와 동일)
    static const qint64 kPeriodSeconds[] = { 600, 3600, 21600, 86400 };
    const bool isToday = currentDate == QDate::currentDate();

    qint64 from, to;
    if (currentPeriodIndex == 3) {
        // "하루"는 롤링 24시간이 아니라 달력 날짜 기준 — 항상 그 날 00:00부터 시작해서
        // 오늘이면 지금까지, 과거 날짜면 그 날 23:59까지.
        // (예전엔 과거 날짜일 때 아래 else 분기의 "to - 86400"을 그대로 타서 자정이 아니라
        //  전날 23:59:59부터 시작하는 버그가 있었다 — 하루만 따로 항상 처리하도록 분리)
        from = QDateTime(currentDate, QTime(0, 0)).toSecsSinceEpoch();
        to = isToday
            ? QDateTime::currentSecsSinceEpoch()
            : QDateTime(currentDate, QTime(23, 59, 59)).toSecsSinceEpoch();
    } else {
        // 10분/1시간/6시간은 "지금 기준 최근 N": 과거 날짜를 조회 중이면 그 날 23:59:59를
        // 끝점으로 삼아 거기서 기간만큼 거슬러 올라간다.
        to = isToday
            ? QDateTime::currentSecsSinceEpoch()
            : QDateTime(currentDate, QTime(23, 59, 59)).toSecsSinceEpoch();
        from = to - kPeriodSeconds[currentPeriodIndex];
    }

    emit sensorLogRequested(currentZoneId, from, to);
}

void GraphPage::loadSensorLogFromServer(const QJsonArray &rows)
{
    if (rows.isEmpty())
        return; // 빈 결과 -> 기존 화면 유지 (아무것도 없다고 빈 그래프로 덮어쓰지 않음)

    QVector<double> gasAvg, gasMax, smokeAvg, smokeMax;
    QStringList timeLabels;
    gasAvg.reserve(rows.size());
    gasMax.reserve(rows.size());
    smokeAvg.reserve(rows.size());
    smokeMax.reserve(rows.size());
    timeLabels.reserve(rows.size());
    sampleTimes.clear();
    sampleTimes.reserve(rows.size());

    for (const QJsonValue &v : rows) {
        const QJsonObject row = v.toObject();
        gasAvg.append(row.value("gasAvg").toDouble());
        gasMax.append(row.value("gasMax").toDouble());
        smokeAvg.append(row.value("smokeAvg").toDouble());
        smokeMax.append(row.value("smokeMax").toDouble());
        const qint64 t = qint64(row.value("t").toDouble());
        sampleTimes.append(t);
        timeLabels.append(QDateTime::fromSecsSinceEpoch(t).toString("HH:mm"));
    }

    // 포인트별로 다 넘겨야(끝점 2개만이 아니라) 그래프에 마우스오버 시 정확한 시각이 뜬다.
    // bucketSec==1(원본)이면 서버가 avg==max로 그대로 보내므로(명세서 3-2), 그때만 단일 선으로 그린다.
    const bool dual = gasAvg != gasMax;
    gasGraph->setSeries(gasAvg, dual ? gasMax : QVector<double>{}, timeLabels);
    smokeGraph->setSeries(smokeAvg, dual ? smokeMax : QVector<double>{}, timeLabels);
    rebuildEventMarkers(); // x축 시각 범위가 바뀌었으니 마커 위치도 다시 계산
    noteLabel->setVisible(false);
}

void GraphPage::setEventMarkersFromServer(const QJsonArray &rows)
{
    eventStamps.clear();
    for (const QJsonValue &v : rows) {
        const QJsonObject row = v.toObject();
        // 안전 복귀/정보성 로그까지 다 찍으면 선이 빽빽해져서 추이가 안 보인다 — 경고/위험만.
        const QString severity = row.value("severity").toString();
        if (severity != "warning" && severity != "danger")
            continue;

        EventStamp stamp;
        stamp.ts = qint64(row.value("ts").toDouble());
        stamp.danger = (severity == "danger");

        const QString cause = causeText(row.value("cause").toString());
        stamp.label = QString("%1 %2%3")
            .arg(QDateTime::fromSecsSinceEpoch(stamp.ts).toString("HH:mm"),
                  stamp.danger ? "위험" : "경고",
                  cause.isEmpty() ? QString() : " · " + cause);
        eventStamps.append(stamp);
    }
    rebuildEventMarkers();
}

void GraphPage::rebuildEventMarkers()
{
    QVector<GraphEventMarker> markers;

    // 표본이 2개 미만이면 x축 범위 자체가 없어서 위치를 잡을 수 없다.
    if (sampleTimes.size() >= 2) {
        const qint64 first = sampleTimes.first();
        const qint64 last = sampleTimes.last();
        if (last > first) {
            for (const EventStamp &stamp : eventStamps) {
                // 지금 보고 있는 시간 범위 밖의 사건은 그냥 버린다(다른 날짜를 보는 중 등).
                if (stamp.ts < first || stamp.ts > last)
                    continue;
                GraphEventMarker m;
                m.xRatio = double(stamp.ts - first) / double(last - first);
                m.danger = stamp.danger;
                m.label = stamp.label;
                markers.append(m);
            }
        }
    }

    gasGraph->setEventMarkers(markers);
    smokeGraph->setEventMarkers(markers);
}

void GraphPage::showQueryFailed(const QString &reason)
{
    noteLabel->setText("⚠ 조회 실패: " + (reason.isEmpty() ? "알 수 없는 오류" : reason));
    noteLabel->setStyleSheet("color:#f87171; font-size:11px;");
    noteLabel->setVisible(true);
}

void GraphPage::updateZone(const Zone &zone)
{
    gasTitleLabel->setText("가스 농도 추이 — " + zone.name + " (ppm)");
    smokeTitleLabel->setText("연기 농도 추이 — " + zone.name + " (ppm)");

    const QString zoneId = zone.name.left(1);
    if (zoneId != currentZoneId) {
        currentZoneId = zoneId;
        requestCurrentPeriod(); // 구역이 실제로 바뀌었을 때만 재조회 (매초 오는 sensor 메시지마다 X)
    }
}
