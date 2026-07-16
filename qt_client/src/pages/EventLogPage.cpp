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
}

EventLogPage::EventLogPage(QWidget *parent)
    : QWidget(parent)
{
    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(16);

    // left column: filter + log list (top), gas graph (bottom)
    auto *leftCol = new QVBoxLayout;

    auto *filterBar = new QHBoxLayout;
    zoneFilterCombo = new QComboBox(this);
    zoneFilterCombo->addItems(kZoneFilterNames);
    auto *zoneLabel = new QLabel("구역:", this);
    zoneLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    filterBar->addWidget(zoneLabel);
    filterBar->addWidget(zoneFilterCombo);

    searchEdit = new QLineEdit(this);
    searchEdit->setPlaceholderText("감지 내용 검색...");
    filterBar->addWidget(searchEdit, 1);

    auto *searchBtn = new QPushButton("조회", this);
    searchBtn->setStyleSheet(QString("background-color:%1; color:white; border-radius:6px; padding:6px 14px;").arg(kAccent));
    filterBar->addWidget(searchBtn);
    leftCol->addLayout(filterBar);
    leftCol->addSpacing(8);

    eventTable = new QTableWidget(0, 4, this);
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
    connect(searchEdit, &QLineEdit::returnPressed, this, &EventLogPage::applyFilter);
    connect(eventTable, &QTableWidget::cellClicked, this, &EventLogPage::showDetail);
}

void EventLogPage::updateZone(const Zone &zone)
{
    const QVector<double> gasSeries =
        zone.state == ZoneState::Safe ? QVector<double>{ 2, 2.5, 3, 2.8, 3.2, 3 }
        : zone.state == ZoneState::Warning ? QVector<double>{ 3, 4, 6, 8, 7, 6 }
                                            : QVector<double>{ 3, 6, 10, 15, 13, 12 };
    gasGraph->setData(gasSeries, { "12:00", "23:00" });
    zoneFilterCombo->setCurrentText(zone.name);
}

void EventLogPage::addEntry(const QString &zone, const QString &detection, const QString &response,
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
}

void EventLogPage::applyFilter()
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
