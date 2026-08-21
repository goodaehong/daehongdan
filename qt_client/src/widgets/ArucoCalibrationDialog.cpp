#include "ArucoCalibrationDialog.h"
#include "../network/ServerLink.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonObject>
#include <QColor>
#include <QFont>
#include <QAbstractItemView>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kBg = "#0d0d16";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";

constexpr int kColChannel = 0, kColStage = 1, kColHint = 2, kColMarkers = 3,
              kColError = 4, kColLens = 5, kColHomography = 6, kColReload = 7;

// 재로드는 완료 응답이 없어 몇 초 뒤 자동으로 다시 조회해서 반영한다(워커가 "다음 프레임"에
// 처리하므로 즉시 조회하면 아직 반영 전일 수 있음).
constexpr int kAutoRefreshDelayMs = 3000;

// 앱 전체가 쓰는 다크 카드 톤에 맞춘 테이블 스타일. 기본 QTableWidget은 다크 배경에서
// 격자선/헤더 구분이 거의 안 보여서(배경색과 grid 색이 비슷) 명시적으로 다 지정한다.
QString tableStyleSheet()
{
    return QString(
        "QTableWidget { background-color:%1; color:%2; border:1px solid %3; border-radius:10px; "
        "gridline-color:%3; font-size:13px; outline:none; }"
        "QTableWidget::item { padding:8px 10px; border-bottom:1px solid %3; }"
        "QTableWidget::item:selected { background-color:%1; color:%2; }"
        "QHeaderView::section { background-color:%4; color:%5; border:none; "
        "border-bottom:2px solid %3; padding:10px; font-weight:bold; font-family:\"hanwhaGothic EL\"; }"
        "QTableCornerButton::section { background-color:%4; border:none; }")
        .arg(kCardBg, kTextPrimary, kCardBorder, kBg, kTextSecondary);
}
}

ArucoCalibrationDialog::ArucoCalibrationDialog(ServerLink *serverLink, QWidget *parent)
    : QDialog(parent), serverLink(serverLink)
{
    setWindowTitle("카메라 좌표 보정 상태 (관리자)");
    setStyleSheet(QString("background-color:%1; color:%2;").arg(kCardBg, kTextPrimary));
    setMinimumWidth(1020);
    setMinimumHeight(520);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 24);
    root->setSpacing(16);

    auto *titleRow = new QHBoxLayout;
    auto *title = new QLabel("카메라 좌표 보정 상태", this);
    title->setStyleSheet(QString("color:%1; font-size:18px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
    titleRow->addWidget(title);
    titleRow->addStretch();
    refreshButton = new QPushButton("↻ 새로고침", this);
    refreshButton->setCursor(Qt::PointingHandCursor);
    refreshButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:white; border:none; border-radius:8px; padding:8px 18px; font-weight:bold; }"
        "QPushButton:hover { background-color:#7c6ce8; }").arg(kAccent));
    connect(refreshButton, &QPushButton::clicked, this, &ArucoCalibrationDialog::onRefreshClicked);
    titleRow->addWidget(refreshButton);
    root->addLayout(titleRow);

    auto *desc = new QLabel(
        "채널별 ArUco 좌표 보정 상태입니다. 좌표 설정 자체는 라즈베리파이에서 SSH로 작업합니다 — "
        "여기서는 진행 상태 확인과, 새로 계산된 보정 파일을 서버 재시작 없이 다시 불러오는 것만 합니다.",
        this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    root->addWidget(desc);

    auto *tableCard = new QFrame(this);
    tableCard->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:10px;").arg(kCardBg, kCardBorder));
    auto *tableCardLayout = new QVBoxLayout(tableCard);
    tableCardLayout->setContentsMargins(1, 1, 1, 1);

    table = new QTableWidget(4, 8, tableCard);
    table->setHorizontalHeaderLabels({"채널", "단계", "안내", "검출/반영 마커", "오차(px)",
                                       "렌즈보정", "Homography", ""});
    table->setStyleSheet(tableStyleSheet());
    table->setShowGrid(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(44);
    table->horizontalHeader()->setHighlightSections(false);
    table->setColumnWidth(kColChannel, 70);
    table->setColumnWidth(kColStage, 140);
    table->setColumnWidth(kColMarkers, 150);
    table->setColumnWidth(kColError, 100);
    table->setColumnWidth(kColLens, 100);
    table->setColumnWidth(kColHomography, 130);
    table->setColumnWidth(kColReload, 110);
    table->horizontalHeader()->setSectionResizeMode(kColChannel, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColStage, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColHint, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(kColMarkers, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColError, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColLens, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColHomography, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColReload, QHeaderView::Fixed);
    table->setMinimumHeight(240);
    for (int row = 0; row < 4; row++)
        table->setItem(row, kColChannel, new QTableWidgetItem(QString("Ch.%1").arg(row + 1)));
    tableCardLayout->addWidget(table);
    root->addWidget(tableCard, 1);

    statusLabel = new QLabel("", this);
    statusLabel->setWordWrap(true);
    statusLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    root->addWidget(statusLabel);

    autoRefreshTimer = new QTimer(this);
    autoRefreshTimer->setSingleShot(true);
    connect(autoRefreshTimer, &QTimer::timeout, this, &ArucoCalibrationDialog::onRefreshClicked);

    connect(serverLink, &ServerLink::calibStatusReceived, this, &ArucoCalibrationDialog::onCalibStatusReceived);
    connect(serverLink, &ServerLink::calibReloadResult, this, &ArucoCalibrationDialog::onCalibReloadResult);

    requestStatus();
}

void ArucoCalibrationDialog::requestStatus()
{
    serverLink->sendQuery("calib_status", QJsonObject());
    statusLabel->setText("불러오는 중...");
}

void ArucoCalibrationDialog::onRefreshClicked()
{
    requestStatus();
}

void ArucoCalibrationDialog::onReloadClicked(int channel)
{
    statusLabel->setText(QString("Ch.%1 재로드 요청 중...").arg(channel));
    serverLink->sendReloadCalibration(channel);
}

void ArucoCalibrationDialog::onCalibReloadResult(int channel, bool accepted, const QString &reason)
{
    if (!accepted) {
        statusLabel->setText(QString("실패: Ch.%1 재로드 요청 거부 — %2")
                                  .arg(channel).arg(reason.isEmpty() ? "알 수 없는 사유" : reason));
        return;
    }
    statusLabel->setText(QString("접수됨: Ch.%1 재로드 — 해당 채널 워커가 다음 프레임에 다시 읽습니다. "
                                  "%2초 후 자동으로 상태를 다시 확인합니다.")
                              .arg(channel).arg(kAutoRefreshDelayMs / 1000));
    autoRefreshTimer->start(kAutoRefreshDelayMs);
}

void ArucoCalibrationDialog::onCalibStatusReceived(const QVector<CalibChannelStatus> &channels)
{
    statusLabel->setText("");
    for (const CalibChannelStatus &s : channels) {
        if (s.channel < 1 || s.channel > 4)
            continue;
        const int row = s.channel - 1;

        auto *channelItem = new QTableWidgetItem(QString("Ch.%1").arg(s.channel));
        channelItem->setTextAlignment(Qt::AlignCenter);
        table->setItem(row, kColChannel, channelItem);

        auto *stageItem = new QTableWidgetItem(s.stage);
        QFont stageFont = stageItem->font();
        stageFont.setBold(true);
        stageItem->setFont(stageFont);
        stageItem->setForeground(QColor(s.ready ? "#34d399" : "#fbbf24"));
        table->setItem(row, kColStage, stageItem);

        auto *hintItem = new QTableWidgetItem(s.hint.isEmpty() ? "-" : s.hint);
        hintItem->setForeground(QColor(kTextSecondary));
        table->setItem(row, kColHint, hintItem);

        auto centeredItem = [](const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            return item;
        };

        if (s.hasLive) {
            table->setItem(row, kColMarkers,
                centeredItem(QString("%1 / %2").arg(s.detectedMarkers).arg(s.acceptedMarkers)));
            table->setItem(row, kColError, centeredItem(QString::number(s.errorPx, 'f', 2)));
            table->setItem(row, kColLens, centeredItem(s.lensApplied ? "적용됨" : "-"));
            table->setItem(row, kColHomography, centeredItem(s.staticHomography ? "있음" : "-"));
        } else {
            table->setItem(row, kColMarkers, centeredItem("-"));
            table->setItem(row, kColError, centeredItem("-"));
            table->setItem(row, kColLens, centeredItem("-"));
            table->setItem(row, kColHomography, centeredItem("-"));
        }

        auto *reloadBtn = new QPushButton("재로드", table);
        reloadBtn->setCursor(Qt::PointingHandCursor);
        reloadBtn->setStyleSheet(QString(
            "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:5px 12px; }"
            "QPushButton:hover { border:1px solid %4; color:%4; }").arg(kBg, kTextPrimary, kCardBorder, kAccent));
        const int channel = s.channel;
        connect(reloadBtn, &QPushButton::clicked, this, [this, channel]() { onReloadClicked(channel); });

        auto *cellWrap = new QWidget(table);
        auto *cellLayout = new QHBoxLayout(cellWrap);
        cellLayout->setContentsMargins(8, 4, 8, 4);
        cellLayout->addWidget(reloadBtn);
        table->setCellWidget(row, kColReload, cellWrap);
    }
}
