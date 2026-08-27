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
        // 자동 전환(노션 확정안 3번): followLatestSegment가 true면(=계속 최신 구간을 보던 중)
        // 분이 바뀔 때마다 최신 구간을 다시 계산해서 자동으로 넘어간다. 과거 구간을 일부러 보고
        // 있는 중이면(◀로 벗어나 followLatestSegment=false) 건드리지 않는다.
        if (currentDate == QDate::currentDate() && followLatestSegment) {
            const int freshLatest = latestSegmentIndexForDate();
            if (freshLatest != currentSegmentIndex) {
                currentSegmentIndex = freshLatest;
                updateSegmentRangeLabel();
                updateNavButtons();
            }
        }
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

    // "이전"/"다음"은 이제 날짜가 아니라 "같은 날짜 안에서 구간 하나씩" 이동한다(노션 확정안).
    // 다른 날짜를 보려면 날짜 선택기를 쓴다 — 그래서 라벨에서 "날"을 뺐다.
    prevButton = new QPushButton("◀️ 이전 구간", bar);
    todayButton = new QPushButton("현재", bar);
    nextButton = new QPushButton("다음 구간 ▶️", bar);
    for (QPushButton *btn : { prevButton, todayButton, nextButton }) {
        btn->setStyleSheet(navBtnStyle);
        row->addWidget(btn);
    }
    connect(prevButton, &QPushButton::clicked, this, [this]() {
        if (currentSegmentIndex <= 0)
            return;
        --currentSegmentIndex;
        followLatestSegment = false; // 최신 구간에서 벗어났으니 자동 전환 중단
        updateSegmentRangeLabel();
        updateNavButtons();
        requestCurrentPeriod();
    });
    connect(nextButton, &QPushButton::clicked, this, [this]() {
        const int latest = latestSegmentIndexForDate();
        if (currentSegmentIndex >= latest)
            return;
        ++currentSegmentIndex;
        followLatestSegment = (currentSegmentIndex >= latest); // 최신 구간까지 돌아왔으면 다시 자동 전환 대상
        updateSegmentRangeLabel();
        updateNavButtons();
        requestCurrentPeriod();
    });
    connect(todayButton, &QPushButton::clicked, this, [this]() {
        currentDate = QDate::currentDate();
        dateButton->setText("📅 " + currentDate.toString("yyyy-MM-dd"));
        resetToLatestSegment();
    });

    row->addStretch();
    outer->addLayout(row);

    segmentRangeLabel = new QLabel(bar);
    segmentRangeLabel->setStyleSheet(QString("color:%1; font-size:12px;").arg(kAccent));
    outer->addWidget(segmentRangeLabel);

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
    currentPeriodIndex = 0;
    periodButtons[0]->setChecked(true);
    legendLabel->setVisible(false);
    resetToLatestSegment();

    return bar;
}

int GraphPage::periodMinutes(int index) const
{
    // 기간 버튼 4개 = 10분/1시간/6시간/하루 (노션 확정안 — 기간 버튼은 "그 날짜 안 구간 크기").
    static const int kMinutes[] = { 10, 60, 360, 1440 };
    return kMinutes[qBound(0, index, 3)];
}

int GraphPage::segmentsPerDay(int index) const
{
    return 1440 / periodMinutes(index);
}

int GraphPage::latestSegmentIndexForDate() const
{
    const bool isToday = currentDate == QDate::currentDate();
    if (!isToday)
        return segmentsPerDay(currentPeriodIndex) - 1; // 과거 날짜는 항상 그 날의 마지막 구간까지
    const QTime now = QTime::currentTime();
    const int minutesSinceMidnight = now.hour() * 60 + now.minute();
    return qBound(0, minutesSinceMidnight / periodMinutes(currentPeriodIndex), segmentsPerDay(currentPeriodIndex) - 1);
}

void GraphPage::resetToLatestSegment()
{
    currentSegmentIndex = latestSegmentIndexForDate();
    followLatestSegment = true;
    updateSegmentRangeLabel();
    updateNavButtons();
    requestCurrentPeriod();
}

void GraphPage::updateSegmentRangeLabel()
{
    if (!segmentRangeLabel)
        return;
    const int minutes = periodMinutes(currentPeriodIndex);
    if (minutes >= 1440) {
        segmentRangeLabel->setText("구간: 하루 전체");
        return;
    }
    const QDateTime from = QDateTime(currentDate, QTime(0, 0)).addSecs(qint64(currentSegmentIndex) * minutes * 60);
    const QDateTime to = from.addSecs(qint64(minutes) * 60);
    segmentRangeLabel->setText(QString("구간: %1 ~ %2").arg(from.toString("HH:mm"), to.toString("HH:mm")));
}

void GraphPage::selectPeriod(int index)
{
    currentPeriodIndex = index;
    for (int i = 0; i < periodButtons.size(); ++i)
        periodButtons[i]->setChecked(i == index);
    // 10분/1시간은 원본 그대로(선 1개), 6시간/하루는 구간 집계(AVG+MAX 두 선)라 범례를 켠다.
    legendLabel->setVisible(index >= 2);
    resetToLatestSegment(); // 구간 크기가 바뀌었으니 그 날짜의 최신 구간으로 다시 잡는다
}

void GraphPage::updateNavButtons()
{
    const bool isToday = currentDate == QDate::currentDate();
    todayButton->setEnabled(!isToday || currentSegmentIndex != latestSegmentIndexForDate());
    prevButton->setEnabled(currentSegmentIndex > 0);
    nextButton->setEnabled(currentSegmentIndex < latestSegmentIndexForDate());
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
        resetToLatestSegment(); // 고른 날짜의 최신(과거면 마지막) 구간부터 보여준다
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

    // 노션 확정안: 기간 버튼은 "지금 기준 최근 N"이 아니라 currentDate 안에서 :00/:10/:20...
    // 정각 경계로 정렬된 구간 크기이고, currentSegmentIndex가 그 날 몇 번째 구간인지를 가리킨다.
    // 예) 10분 구간, currentSegmentIndex=86이면 그 날 14:20~14:30 — 오늘이고 지금이 14:23이면
    // 서버엔 그대로 14:20~14:30을 요청하고(정렬 유지), 14:23 이후 데이터가 없을 뿐이다.
    const int minutes = periodMinutes(currentPeriodIndex);
    const qint64 from = QDateTime(currentDate, QTime(0, 0))
        .addSecs(qint64(currentSegmentIndex) * minutes * 60).toSecsSinceEpoch();
    const qint64 to = from + qint64(minutes) * 60;
    // 진행 중인 구간(예: 14:20~14:30인데 지금 14:23)이어도 경계를 그대로 요청한다 — 서버는
    // 어차피 지금까지 쌓인 데이터만 돌려주고, 그래프는 남은 구간을 오른쪽에 빈 채로 그린다
    // (currentSegmentRangeFrom/To로 그대로 넘겨서 축 정렬에 쓴다).
    currentSegmentRangeFrom = from;
    currentSegmentRangeTo = to;

    emit sensorLogRequested(currentZoneId, from, to);
}

void GraphPage::loadSensorLogFromServer(const QJsonArray &rows)
{
    if (rows.isEmpty()) {
        // 예전엔 그냥 return해서 이전 화면을 그대로 유지했는데, 구간 개념이 생긴 지금은 "다른
        // 구간을 보는 중인데 이전 구간 그래프가 그대로 남아있다"는 혼동을 준다(구간 라벨은
        // 새 구간을 가리키는데 그래프는 옛 구간 데이터 — 서버가 안 죽고 그냥 그 구간에 데이터가
        // 없는 것뿐인데 마치 버그처럼 보임). 빈 결과도 명시적으로 반영해서 그래프를 비운다.
        sampleTimes.clear();
        gasGraph->setSeries({}, {}, {}, {}, currentSegmentRangeFrom, currentSegmentRangeTo);
        smokeGraph->setSeries({}, {}, {}, {}, currentSegmentRangeFrom, currentSegmentRangeTo);
        rebuildEventMarkers();
        noteLabel->setText("이 구간에는 데이터가 없습니다.");
        noteLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
        noteLabel->setVisible(true);
        return;
    }

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
    // sampleTimes/currentSegmentRangeFrom·To를 같이 넘겨서, 진행 중인 구간의 아직 안 지난
    // 오른쪽 부분이 빈 채로 보이도록 한다(노션 확정안 — 정각 정렬 구간).
    const bool dual = gasAvg != gasMax;
    gasGraph->setSeries(gasAvg, dual ? gasMax : QVector<double>{}, timeLabels,
                         sampleTimes, currentSegmentRangeFrom, currentSegmentRangeTo);
    smokeGraph->setSeries(smokeAvg, dual ? smokeMax : QVector<double>{}, timeLabels,
                           sampleTimes, currentSegmentRangeFrom, currentSegmentRangeTo);
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
        stamp.gasPpm = row.value("gasPpm").toDouble();
        stamp.smokePpm = row.value("smokePpm").toDouble();

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
    // 가스/연기 그래프가 마커 리스트를 공유하면 서로 다른 농도값을 보여줄 수 없어서
    // (한쪽 그래프에 마우스 올렸는데 다른 쪽 농도가 뜨면 안 됨) 그래프별로 따로 만든다.
    QVector<GraphEventMarker> gasMarkers, smokeMarkers;

    // 그래프 데이터 선이 이제 구간 경계(currentSegmentRangeFrom~To) 기준 시간 비율로 그려지므로,
    // 마커도 같은 기준을 써야 데이터 선과 위치가 어긋나지 않는다(예전엔 표본 첫/끝 시각 기준이라
    // 진행 중인 구간에서 서로 다른 축척을 쓰게 되는 문제가 있었다).
    if (currentSegmentRangeTo > currentSegmentRangeFrom) {
        for (const EventStamp &stamp : eventStamps) {
            // 지금 보고 있는 구간 밖의 사건은 그냥 버린다(다른 날짜/구간을 보는 중 등).
            if (stamp.ts < currentSegmentRangeFrom || stamp.ts > currentSegmentRangeTo)
                continue;
            const double xRatio = double(stamp.ts - currentSegmentRangeFrom) / double(currentSegmentRangeTo - currentSegmentRangeFrom);

            GraphEventMarker gm;
            gm.xRatio = xRatio;
            gm.danger = stamp.danger;
            gm.label = stamp.label + QString(" (%1ppm)").arg(stamp.gasPpm, 0, 'f', 1);
            gasMarkers.append(gm);

            GraphEventMarker sm;
            sm.xRatio = xRatio;
            sm.danger = stamp.danger;
            sm.label = stamp.label + QString(" (%1ppm)").arg(stamp.smokePpm, 0, 'f', 1);
            smokeMarkers.append(sm);
        }
    }

    gasGraph->setEventMarkers(gasMarkers);
    smokeGraph->setEventMarkers(smokeMarkers);
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
