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
#include <QJsonArray>
#include <QJsonObject>
#include <QCalendarWidget>
#include <QTimeEdit>
#include <QAbstractSpinBox>
#include <QTextCharFormat>
#include <QTimer>
#include <QMap>

namespace {
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kAccent = "#8b7cf6";
const QStringList kZoneFilterNames = { "전체", "A공장", "B공장", "C공장", "D공장" };
const QStringList kSeverityFilterNames = { "전체", "안전", "경고", "위험" };
const QStringList kPeriodFilterNames = { "전체 기간", "최근 1시간", "최근 6시간", "최근 24시간" };
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
// 기존엔 밝은 색을 낮은 알파로 어두운 배경 위에 얹어서 탁한 카키/갈색으로 보였음.
// 채도 높은 색(rose/amber) + 알파를 조금 올려서 와인레드/골드 톤이 또렷하게 드러나도록 조정.
QColor rowTintForSeverity(const QString &severity)
{
    if (severity == "위험") return QColor(244, 63, 94, 70);   // 와인레드
    if (severity == "경고") return QColor(252, 211, 77, 70);  // 허니 골드 (R·G값을 가깝게 둬서 주황/갈색 느낌을 피함)
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

// server/server_main.cpp respToText()가 만드는 "siren_on,valve_close,fan_off" 형식의 원문을
// 한글로 옮긴다. DB엔 이 원문 그대로 저장돼야 나중에 LIKE 검색/집계가 문구 변경에 안 깨지므로,
// 화면 표시 단계인 여기서만 변환한다 — cause/category를 이미 같은 방식으로 처리 중.
QString translateResponseToken(const QString &token)
{
    static const QMap<QString, QString> kTokenText = {
        { "siren_on", "사이렌 ON" }, { "siren_off", "사이렌 OFF" },
        { "valve_open", "밸브 열림" }, { "valve_close", "밸브 잠금" },
        { "fan_off", "팬 정지" }, { "fan_low", "팬 약" }, { "fan_mid", "팬 중" }, { "fan_high", "팬 강" },
    };
    // 매핑에 없는 토큰(구버전/새 필드 등)은 원문 그대로 둬서 정보가 사라지지 않게 한다.
    return kTokenText.value(token, token);
}

QString translateResponse(const QString &raw)
{
    if (raw.isEmpty())
        return raw;
    QStringList parts;
    for (const QString &token : raw.split(',', Qt::SkipEmptyParts))
        parts << translateResponseToken(token.trimmed());
    return parts.join(" · ");
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
    auto *zoneLabel = new QLabel("공장:", this);
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

    // 날짜 - 시간(프리셋+직접 입력) - 위험도 - 처리상태 순서로 한 줄에 배치.
    auto *filterRow2 = new QHBoxLayout;

    auto *dateLabel = new QLabel("날짜:", this);
    dateLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    filterRow2->addWidget(dateLabel);
    dateButton = new QPushButton("전체 날짜", this);
    dateButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:4px 10px; }"
        "QPushButton:hover { border:1px solid %4; }").arg(kCardBg, kTextPrimary, kCardBorder, kAccent));
    dateButton->setToolTip("클릭하면 달력에서 특정 날짜를 고를 수 있습니다");
    filterRow2->addWidget(dateButton);
    auto *clearDateBtn = new QPushButton("✕", this);
    clearDateBtn->setFixedWidth(28);
    clearDateBtn->setToolTip("날짜 필터 초기화");
    clearDateBtn->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; }"
        "QPushButton:hover { border:1px solid %4; color:%4; }").arg(kCardBg, kTextSecondary, kCardBorder, kAccent));
    filterRow2->addWidget(clearDateBtn);

    auto *timeLabel = new QLabel("시간:", this);
    timeLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    filterRow2->addWidget(timeLabel);
    periodFilterCombo = new QComboBox(this);
    periodFilterCombo->addItems(kPeriodFilterNames);
    periodFilterCombo->setStyleSheet(kComboStyle);
    filterRow2->addWidget(periodFilterCombo);
    const QString timeEditStyle = QString(
        "QTimeEdit { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:4px 6px; }"
        "QTimeEdit:focus { border:1px solid %4; }").arg(kCardBg, kTextPrimary, kCardBorder, kAccent);
    startTimeEdit = new QTimeEdit(QTime(0, 0), this);
    startTimeEdit->setDisplayFormat("HH:mm");
    startTimeEdit->setStyleSheet(timeEditStyle);
    startTimeEdit->setFocusPolicy(Qt::StrongFocus);
    startTimeEdit->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    filterRow2->addWidget(startTimeEdit);
    auto *tildeLabel = new QLabel("~", this);
    tildeLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    filterRow2->addWidget(tildeLabel);
    endTimeEdit = new QTimeEdit(QTime(23, 59), this);
    endTimeEdit->setDisplayFormat("HH:mm");
    endTimeEdit->setStyleSheet(timeEditStyle);
    endTimeEdit->setFocusPolicy(Qt::StrongFocus);
    endTimeEdit->setButtonSymbols(QAbstractSpinBox::UpDownArrows);
    filterRow2->addWidget(endTimeEdit);

    auto *severityLabel = new QLabel("위험도:", this);
    severityLabel->setStyleSheet(QString("color:%1;").arg(kTextSecondary));
    severityFilterCombo = new QComboBox(this);
    severityFilterCombo->addItems(kSeverityFilterNames);
    severityFilterCombo->setStyleSheet(kComboStyle);
    filterRow2->addWidget(severityLabel);
    filterRow2->addWidget(severityFilterCombo);

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

    eventTable = new QTableWidget(0, 6, this);
    eventTable->setHorizontalHeaderLabels({ "날짜", "시간", "공장", "감지 내용", "대응 결과", "처리상태" });
    eventTable->horizontalHeader()->setStretchLastSection(true);
    // 이전엔 ResizeToContents라 사용자가 컬럼 폭을 직접 조절할 수 없었음 -> Interactive로 바꿔서
    // 드래그로 셀 크기를 조절할 수 있게 함. 초기 폭은 아래 resizeColumnsToContents()로 한 번 맞춰줌.
    eventTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 스크롤바도 다른 화면들과 동일한 다크/퍼플 accent 테마로 맞춤.
    eventTable->setStyleSheet(QString(
        "QTableWidget { background-color:%1; color:%2; border:1px solid %3; gridline-color:%3; }"
        "QHeaderView::section { background-color:#1a1a26; color:%4; border:none; padding:6px; }"
        "QScrollBar:vertical { background:#14141f; width:10px; margin:0; border-radius:5px; }"
        "QScrollBar::handle:vertical { background:#3a3550; min-height:24px; border-radius:5px; }"
        "QScrollBar::handle:vertical:hover { background:#8b7cf6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; border:none; background:none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:none; }"
        "QScrollBar:horizontal { background:#14141f; height:10px; margin:0; border-radius:5px; }"
        "QScrollBar::handle:horizontal { background:#3a3550; min-width:24px; border-radius:5px; }"
        "QScrollBar::handle:horizontal:hover { background:#8b7cf6; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; border:none; background:none; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:none; }")
        .arg(kCardBg, kTextPrimary, kCardBorder, kTextSecondary));
    leftCol->addWidget(eventTable, 3);

    auto *graphLabel = new QLabel("가스 농도 추이", this);
    graphLabel->setStyleSheet(QString("color:%1; font-size:13px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
    leftCol->addSpacing(8);
    leftCol->addWidget(graphLabel);
    gasGraph = new GasGraphWidget(this);
    gasGraph->setLineColor(QColor(kAccent));
    gasGraph->setUnit("ppm");
    gasGraph->setThresholds(200, 2000); // MQ-9 LPG 곡선 기준: 경고(전광판 주황) 200ppm / 위험 2000ppm (judgement.cpp)
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
        valueLabel->setStyleSheet(QString("color:%1; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
        valueLabel->setWordWrap(true);
        row->addWidget(nameLabel);
        row->addWidget(valueLabel, 1);
        detailLayout->addLayout(row);
        if (valueOut)
            *valueOut = valueLabel;
    };

    addDetailRow("발생 시간", &detailTimeValue);
    addDetailRow("공장", &detailZoneValue);
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
    connect(dateButton, &QPushButton::clicked, this, &EventLogPage::showDatePicker);
    connect(clearDateBtn, &QPushButton::clicked, this, &EventLogPage::clearDateFilter);
    connect(startTimeEdit, &QTimeEdit::timeChanged, this, &EventLogPage::applyFilter);
    connect(endTimeEdit, &QTimeEdit::timeChanged, this, &EventLogPage::applyFilter);
}

void EventLogPage::updateZone(const Zone &zone)
{
    if (zone.hasLiveSensorData && zone.gasHistory.size() >= 2) {
        // 실측 이력이 쌓여있으면 그걸로 실시간 추이를 그린다. 포인트별 라벨을 다 넘겨야
        // 그래프 위에 마우스 올렸을 때 정확한 시각이 뜬다.
        gasGraph->setData(zone.gasHistory, zone.gasHistoryLabels);
    } else {
        // 실측 데이터가 아직 없는 구역(DEMO)은 기존처럼 상태 기반 가짜 패턴을 보여준다.
        // 값 범위는 judgement.cpp 실측 임계값(경고 200ppm / 위험 2000ppm)에 맞춰 조정.
        const QVector<double> gasSeries =
            zone.state == ZoneState::Safe ? QVector<double>{ 30, 35, 32, 38, 34, 36 }
            : zone.state == ZoneState::Warning ? QVector<double>{ 60, 120, 180, 220, 190, 150 }
                                                : QVector<double>{ 300, 900, 1800, 2500, 2200, 1900 };
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
    entry.date = entry.timestamp.toString("yyyy-MM-dd");
    entry.time = entry.timestamp.toString("HH:mm:ss");
    entry.zone = zone;
    entry.detection = detection;
    entry.response = response;
    entry.admin = admin;
    entry.severity = severity;
    entry.sensorCombo = sensorCombo;
    entry.status = "해결됨";
    entry.duration = duration;
    appendRow(entry);
    applyFilter(); // 새 항목도 현재 필터 조건에 맞춰 바로 숨김/표시 반영
}

void EventLogPage::loadEntriesFromServer(const QJsonArray &rows)
{
    eventEntries.clear();
    eventTable->setRowCount(0);

    // 서버는 최신순(ts DESC)으로 보내주는데(명세서 3-1), 그대로 위에서부터 채우면 맨 아래가 가장
    // 오래된 항목이 되어 scrollToBottom()이 오히려 옛날 항목을 보여주게 된다. 역순으로 채워서
    // 오래된 게 위, 최신이 아래로 가게 하면 기존 scrollToBottom()이 최신 항목을 자연스럽게 보여준다.
    for (int i = rows.size() - 1; i >= 0; --i) {
        const QJsonObject row = rows[i].toObject();

        EventEntry entry;
        entry.timestamp = QDateTime::fromSecsSinceEpoch(qint64(row.value("ts").toDouble()));
        entry.date = entry.timestamp.toString("yyyy-MM-dd");
        entry.time = entry.timestamp.toString("HH:mm:ss");
        // 서버는 zone을 "A" 한 글자로 주는데, 필터 콤보/화면 표기는 "A공장" 형태라 맞춰준다.
        const QString zoneCode = row.value("zone").toString();
        entry.zone = zoneCode.isEmpty() ? zoneCode : zoneCode + "공장";

        const QString category = row.value("category").toString();
        const QString causePhrase = causeText(row.value("cause").toString());
        if (!causePhrase.isEmpty()) {
            // emergency/emergency_reapply는 원인은 자동 위험과 같은 값이라 원인 문구만으론 구분이 안 됨 —
            // 수동 조작이었다는 걸 라벨로 명시한다 (emergency-mode #18). 실제 자동/수동 구분은 "관리자" 칸도 참고.
            entry.detection = category == "emergency" ? "[위험 전환] " + causePhrase
                             : category == "emergency_reapply" ? "[대응 재실행] " + causePhrase
                             : causePhrase;
        } else {
            // cause가 없는 이벤트(수동제어/해제 등)는 category 코드를 한글 문구로 변환.
            entry.detection = category == "resolve" ? "위험 해제"
                             : category == "manual_control" ? "관리자 수동 제어"
                             : category == "warning" ? "경고 상태 감지"
                             : category == "danger" ? "위험 상태 감지"
                             : category == "emergency_clear" ? "위험 모드 해제"
                             : category.isEmpty() ? "-" : category;
        }
        entry.response = translateResponse(row.value("response").toString());
        const QString admin = row.value("admin").toString();
        entry.admin = admin.isEmpty() ? "시스템(자동)" : admin;

        // 서버 severity는 "safe"/"warning"/"danger" 코드값 — Qt 표시 문구로 변환.
        const QString severityCode = row.value("severity").toString();
        entry.severity = severityCode == "danger" ? "위험"
                        : severityCode == "warning" ? "경고"
                        : severityCode == "safe" ? "안전"
                        : severityCode.isEmpty() ? "정보" : severityCode;

        entry.sensorCombo = row.value("sensorCombo").toString();
        if (entry.sensorCombo.isEmpty())
            entry.sensorCombo = "-";
        entry.status = row.value("status").toString();
        if (entry.status.isEmpty())
            entry.status = "해결됨";

        const double durationMs = row.value("durationMs").toDouble();
        entry.duration = durationMs > 0 ? QString::number(durationMs / 1000.0, 'f', 0) + "초" : "-";

        appendRow(entry);
    }

    // 처음 로드될 때만 내용에 맞춰 컬럼 폭을 잡아준다. 이후엔 사용자가 드래그로 조절한 폭이
    // 유지되도록 매 행마다 다시 호출하지 않는다(Interactive resize mode).
    eventTable->resizeColumnsToContents();
    applyFilter();
}

void EventLogPage::appendRow(const EventEntry &entry)
{
    eventEntries.append(entry);

    const int row = eventTable->rowCount();
    eventTable->insertRow(row);

    auto *dateItem = new QTableWidgetItem(entry.date);
    auto *timeItem = new QTableWidgetItem(entry.time);
    auto *zoneItem = new QTableWidgetItem(entry.zone);
    auto *detectionItem = new QTableWidgetItem(entry.detection);
    auto *responseItem = new QTableWidgetItem(entry.response);
    auto *statusItem = new QTableWidgetItem(entry.status);

    const QColor rowTint = rowTintForSeverity(entry.severity);
    QTableWidgetItem *items[] = { dateItem, timeItem, zoneItem, detectionItem, responseItem, statusItem };
    for (QTableWidgetItem *item : items) {
        if (rowTint.alpha() > 0)
            item->setBackground(rowTint);
    }
    responseItem->setForeground(responseTextColor(entry.response));

    eventTable->setItem(row, 0, dateItem);
    eventTable->setItem(row, 1, timeItem);
    eventTable->setItem(row, 2, zoneItem);
    eventTable->setItem(row, 3, detectionItem);
    eventTable->setItem(row, 4, responseItem);
    eventTable->setItem(row, 5, statusItem);
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
    if (auto *item = eventTable->item(selectedEventRow, 4))
        item->setText("오탐 처리됨");
}

void EventLogPage::showDatePicker()
{
    // GraphPage의 날짜 팝업과 동일한 구성: 버튼 바로 아래에 뜨는 Qt::Popup 달력.
    auto *popup = new QFrame(this, Qt::Popup);
    popup->setAttribute(Qt::WA_DeleteOnClose);
    popup->setStyleSheet("background-color:#14141f; border:1px solid #333344; border-radius:8px;");

    auto *layout = new QVBoxLayout(popup);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *calendar = new QCalendarWidget(popup);
    calendar->setGridVisible(true);
    calendar->setMaximumDate(QDate::currentDate());
    calendar->setSelectedDate(filterDate.isValid() ? filterDate : QDate::currentDate());
    calendar->setStyleSheet(
        "QCalendarWidget { background-color:#14141f; color:#f5f5fa; }"
        "QCalendarWidget QToolButton { color:#f5f5fa; background-color:transparent; font-size:14px; icon-size:20px; padding:6px; }"
        "QCalendarWidget QToolButton:hover { background-color:#232333; border-radius:6px; }"
        "QCalendarWidget QMenu { background-color:#1a1a26; color:#f5f5fa; }"
        "QCalendarWidget QSpinBox { background-color:#1a1a26; color:#f5f5fa; }"
        "QCalendarWidget QAbstractItemView:enabled { background-color:#14141f; color:#f5f5fa; }"
        "QCalendarWidget QAbstractItemView::item:selected { border:2px solid #a78bfa; "
        "background-color:#8b7cf6; color:white; border-radius:4px; }"
        "QCalendarWidget QAbstractItemView:disabled { color:#4a4658; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background-color:#1a1a26; }");

    QTextCharFormat todayFormat;
    todayFormat.setFontWeight(QFont::Bold);
    todayFormat.setForeground(QColor(kAccent));
    calendar->setDateTextFormat(QDate::currentDate(), todayFormat);

    layout->addWidget(calendar);

    auto pickDate = [this, popup](const QDate &date) {
        filterDate = date;
        dateButton->setText("📅 " + filterDate.toString("yyyy-MM-dd"));
        applyFilter();
        QTimer::singleShot(150, popup, [popup]() { popup->close(); });
    };
    connect(calendar, &QCalendarWidget::clicked, popup, pickDate);
    connect(calendar, &QCalendarWidget::activated, popup, pickDate);

    popup->move(dateButton->mapToGlobal(QPoint(0, dateButton->height() + 4)));
    popup->show();
}

void EventLogPage::clearDateFilter()
{
    filterDate = QDate();
    dateButton->setText("전체 날짜");
    applyFilter();
}

void EventLogPage::applyFilter()
{
    const QString zone = zoneFilterCombo->currentText();
    const QString severityFilter = severityFilterCombo->currentText();
    const QString periodFilter = periodFilterCombo->currentText();
    const QString statusFilter = statusFilterCombo->currentText();
    const QString keyword = searchEdit->text().trimmed();
    const QDateTime now = QDateTime::currentDateTime();
    const QTime rangeStart = startTimeEdit->time();
    const QTime rangeEnd = endTimeEdit->time();

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
        else if (periodFilter == "최근 6시간")
            periodMatch = entry.timestamp.secsTo(now) <= 6 * 3600;
        else if (periodFilter == "최근 24시간")
            periodMatch = entry.timestamp.secsTo(now) <= 24 * 3600;

        // 날짜 팝업으로 고른 특정 날짜(있으면) + 시:분 직접 입력 범위는 위 기간 필터와 별개로
        // AND 조건으로 함께 적용된다. 초 단위 차이로 경계값이 어긋나지 않도록 분 단위로만 비교.
        const bool dateMatch = !filterDate.isValid() || entry.timestamp.date() == filterDate;
        const QTime entryTimeOfDay(entry.timestamp.time().hour(), entry.timestamp.time().minute());
        const bool timeMatch = rangeStart <= rangeEnd
            ? (entryTimeOfDay >= rangeStart && entryTimeOfDay <= rangeEnd)
            : (entryTimeOfDay >= rangeStart || entryTimeOfDay <= rangeEnd); // 자정 넘어가는 범위(예: 22:00~02:00)

        const bool statusMatch = (statusFilter == "전체") || (entry.status == statusFilter);

        const bool keywordMatch = keyword.isEmpty()
            || entry.detection.contains(keyword, Qt::CaseInsensitive)
            || entry.response.contains(keyword, Qt::CaseInsensitive);

        eventTable->setRowHidden(row, !(zoneMatch && severityMatch && periodMatch && dateMatch && timeMatch
                                         && statusMatch && keywordMatch));
    }
}
