#include "EventLogPage.h"
#include "../widgets/GasGraphWidget.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QDateTime>

namespace {
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";
const QStringList kZoneFilterNames = { "전체", "A구역", "B구역" };
const QStringList kSeverityFilterNames = { "전체", "안전", "경고", "위험" };
const QStringList kPeriodFilterNames = { "전체 기간", "최근 1시간", "최근 24시간", "오늘" };
const QStringList kStatusFilterNames = { "전체", "해결됨", "오탐 처리됨" };

const QString kComboStyle = QString(
    "QComboBox { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:4px 8px; }"
    "QComboBox:hover { border:1px solid %4; }"
    "QComboBox::drop-down { border:none; width:20px; }"
    "QComboBox QAbstractItemView { background-color:%1; color:%2; border:1px solid %3; selection-background-color:%4; }")
    .arg(kCardBg, kTextPrimary, kCardBorder, kAccent);

const QString kLineEditStyle = QString(
    "QLineEdit { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:4px 8px; }"
    "QLineEdit:focus { border:1px solid %4; }")
    .arg(kCardBg, kTextPrimary, kCardBorder, kAccent);

// 위험도별 행 배경 강조. 안전/정보는 강조 없음(투명), 경고/위험만 은은하게.
QColor rowTintForSeverity(const QString &severity)
{
    if (severity == "위험") return QColor(248, 113, 113, 40);
    if (severity == "경고") return QColor(251, 191, 36, 34);
    return QColor(0, 0, 0, 0);
}

// 대응 결과 문구로 관리자의 실제 대응 여부를 색으로 구분(휴리스틱: 문구 내 키워드 기준).
QColor responseTextColor(const QString &response)
{
    if (response.contains("무응답") || response.contains("실패"))
        return QColor("#f87171");
    if (response.contains("확인") || response.contains("완료") || response.contains("클릭"))
        return QColor("#34d399");
    return QColor(kTextPrimary);
}
}

EventLogPage::EventLogPage(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // left column: filter + log list (top), gas graph (bottom)
    auto *leftCol = new QVBoxLayout;

    auto *filterRow1 = new QHBoxLayout;
    auto *zoneLabel = new QLabel("구역:", this);
    zoneLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    zoneFilterCombo = new QComboBox(this);
    zoneFilterCombo->addItems(kZoneFilterNames);
    zoneFilterCombo->setStyleSheet(kComboStyle);
    filterRow1->addWidget(zoneLabel);
    filterRow1->addWidget(zoneFilterCombo);

    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("감지 내용/대응 결과 검색...");
    searchEdit->setStyleSheet(kLineEditStyle);
    filterRow1->addWidget(searchEdit, 1);

    auto *searchBtn = new QPushButton("조회", this);
    searchBtn->setStyleSheet(QString("background-color:%1; color:white; border-radius:6px; padding:6px 14px;").arg(kAccent));
    filterRow1->addWidget(searchBtn);
    leftCol->addLayout(filterRow1);

    auto *filterRow2 = new QHBoxLayout;
    auto *severityLabel = new QLabel("위험도:", this);
    severityLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    severityFilterCombo = new QComboBox(this);
    severityFilterCombo->addItems(kSeverityFilterNames);
    severityFilterCombo->setStyleSheet(kComboStyle);
    filterRow2->addWidget(severityLabel);
    filterRow2->addWidget(severityFilterCombo);

    auto *periodLabel = new QLabel("기간:", this);
    periodLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    periodFilterCombo = new QComboBox(this);
    periodFilterCombo->addItems(kPeriodFilterNames);
    periodFilterCombo->setStyleSheet(kComboStyle);
    filterRow2->addWidget(periodLabel);
    filterRow2->addWidget(periodFilterCombo);

    auto *statusLabel = new QLabel("처리 상태:", this);
    statusLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    statusFilterCombo = new QComboBox(this);
    statusFilterCombo->addItems(kStatusFilterNames);
    statusFilterCombo->setStyleSheet(kComboStyle);
    filterRow2->addWidget(statusLabel);
    filterRow2->addWidget(statusFilterCombo);
    filterRow2->addStretch();
    leftCol->addLayout(filterRow2);
    leftCol->addSpacing(8);

    eventTable = new QTableWidget(0, 5, this);
    eventTable->setHorizontalHeaderLabels({ "시간", "구역", "감지 내용", "대응 결과", "처리상태" });
    eventTable->horizontalHeader()->setStretchLastSection(true);
    eventTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    eventTable->setStyleSheet(QString(
        "QTableWidget { background-color:%1; color:%2; border:1px solid %3; gridline-color:%3; }"
        "QHeaderView::section { background-color:#1a1a26; color:%4; border:none; padding:6px; }")
        .arg(kCardBg, kTextPrimary, kCardBorder, kTextSecondary));
    leftCol->addWidget(eventTable, 3);

    auto *graphLabel = new QLabel("가스 농도 추이", this);
    graphLabel->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold;").arg(kTextPrimary));
    leftCol->addSpacing(8);
    leftCol->addWidget(graphLabel);
    gasGraph = new GasGraphWidget(this);
    gasGraph->setLineColor(QColor(kAccent));
    leftCol->addWidget(gasGraph, 2);

    mainLayout->addLayout(leftCol, 2);

    // right column: detail panel
    auto *detailFrame = new QFrame(this);
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
    connect(falseAlarmButton, &QPushButton::clicked, this, &EventLogPage::markFalseAlarm);
    detailLayout->addWidget(falseAlarmButton);
    detailLayout->addStretch();

    detailContent->hide();
    detailOuter->addWidget(detailContent);

    mainLayout->addWidget(detailFrame, 1);

    connect(searchBtn, &QPushButton::clicked, this, &EventLogPage::applyFilter);
    connect(zoneFilterCombo, &QComboBox::currentIndexChanged, this, &EventLogPage::applyFilter);
    connect(severityFilterCombo, &QComboBox::currentIndexChanged, this, &EventLogPage::applyFilter);
    connect(periodFilterCombo, &QComboBox::currentIndexChanged, this, &EventLogPage::applyFilter);
    connect(statusFilterCombo, &QComboBox::currentIndexChanged, this, &EventLogPage::applyFilter);
    connect(searchEdit, &QLineEdit::returnPressed, this, &EventLogPage::applyFilter);
    connect(eventTable, &QTableWidget::cellClicked, this, &EventLogPage::showDetail);
}

void EventLogPage::updateZone(const Zone &zone)
{
    if (zone.hasLiveSensorData && zone.gasHistory.size() >= 2) {
        // 실측 이력이 쌓여있으면 그걸로 실시간 추이를 그린다.
        gasGraph->setData(zone.gasHistory, { zone.gasHistoryLabels.first(), zone.gasHistoryLabels.last() });
    } else {
        // 실측 데이터가 아직 없는 구역(DEMO)은 기존처럼 상태 기반 가짜 패턴을 보여준다.
        const QVector<double> gasSeries =
            zone.state == ZoneState::Safe ? QVector<double>{ 2, 2.5, 3, 2.8, 3.2, 3 }
            : zone.state == ZoneState::Warning ? QVector<double>{ 3, 4, 6, 8, 7, 6 }
                                                : QVector<double>{ 3, 6, 10, 15, 13, 12 };
        gasGraph->setData(gasSeries, { "12:00", "23:00" });
    }
    zoneFilterCombo->setCurrentText(zone.name);
}

void EventLogPage::addEntry(const QString &zone, const QString &detection, const QString &response,
                             const QString &admin, const QString &severity,
                             const QString &sensorCombo, const QString &duration)
{
    EventEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.time = entry.timestamp.toString("HH:mm:ss");
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

    auto *timeItem = new QTableWidgetItem(entry.time);
    auto *zoneItem = new QTableWidgetItem(entry.zone);
    auto *detectionItem = new QTableWidgetItem(entry.detection);
    auto *responseItem = new QTableWidgetItem(entry.response);
    auto *statusItem = new QTableWidgetItem(entry.status);

    const QColor rowTint = rowTintForSeverity(entry.severity);
    QTableWidgetItem *items[] = { timeItem, zoneItem, detectionItem, responseItem, statusItem };
    for (QTableWidgetItem *item : items) {
        if (rowTint.alpha() > 0)
            item->setBackground(rowTint);
    }
    responseItem->setForeground(responseTextColor(entry.response));

    eventTable->setItem(row, 0, timeItem);
    eventTable->setItem(row, 1, zoneItem);
    eventTable->setItem(row, 2, detectionItem);
    eventTable->setItem(row, 3, responseItem);
    eventTable->setItem(row, 4, statusItem);
    eventTable->scrollToBottom();

    applyFilter(); // 새 항목도 현재 필터 조건에 맞춰 바로 숨김/표시 반영
}

void EventLogPage::showDetail(int row, int)
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

void EventLogPage::markFalseAlarm()
{
    if (selectedEventRow < 0 || selectedEventRow >= eventEntries.size())
        return;
    eventEntries[selectedEventRow].status = "오탐 처리됨";
    detailStatusValue->setText("오탐 처리됨");
    if (auto *item = eventTable->item(selectedEventRow, 4))
        item->setText("오탐 처리됨");
}

void EventLogPage::applyFilter()
{
    const QString zone = zoneFilterCombo->currentText();
    const QString severityFilter = severityFilterCombo->currentText();
    const QString periodFilter = periodFilterCombo->currentText();
    const QString statusFilter = statusFilterCombo->currentText();
    const QString keyword = searchEdit->text().trimmed();
    const QDateTime now = QDateTime::currentDateTime();

    for (int row = 0; row < eventTable->rowCount() && row < eventEntries.size(); ++row) {
        const EventEntry &entry = eventEntries[row];

        const bool zoneMatch = (zone == "전체") || (entry.zone == zone);

        bool severityMatch = true;
        if (severityFilter != "전체") {
            // "정보"는 위험도 표기가 없는 성공/확인성 로그라 "안전"과 같은 취급으로 묶는다.
            severityMatch = severityFilter == "안전"
                ? (entry.severity == "안전" || entry.severity == "정보")
                : (entry.severity == severityFilter);
        }

        bool periodMatch = true;
        if (periodFilter == "최근 1시간")
            periodMatch = entry.timestamp.secsTo(now) <= 3600;
        else if (periodFilter == "최근 24시간")
            periodMatch = entry.timestamp.secsTo(now) <= 24 * 3600;
        else if (periodFilter == "오늘")
            periodMatch = entry.timestamp.date() == now.date();

        const bool statusMatch = (statusFilter == "전체") || (entry.status == statusFilter);

        const bool keywordMatch = keyword.isEmpty()
            || entry.detection.contains(keyword, Qt::CaseInsensitive)
            || entry.response.contains(keyword, Qt::CaseInsensitive);

        eventTable->setRowHidden(row, !(zoneMatch && severityMatch && periodMatch && statusMatch && keywordMatch));
    }
}
