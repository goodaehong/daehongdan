#include "ArucoCalibrationDialog.h"
#include "../network/ServerLink.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QJsonObject>
#include <QColor>
#include <QAbstractItemView>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";

constexpr int kColChannel = 0, kColStage = 1, kColHint = 2, kColMarkers = 3,
              kColError = 4, kColLens = 5, kColHomography = 6, kColReload = 7;

// 재로드는 완료 응답이 없어 몇 초 뒤 자동으로 다시 조회해서 반영한다(워커가 "다음 프레임"에
// 처리하므로 즉시 조회하면 아직 반영 전일 수 있음).
constexpr int kAutoRefreshDelayMs = 3000;
}

ArucoCalibrationDialog::ArucoCalibrationDialog(ServerLink *serverLink, QWidget *parent)
    : QDialog(parent), serverLink(serverLink)
{
    setWindowTitle("카메라 좌표 보정 상태 (관리자)");
    setStyleSheet(QString("background-color:%1; color:%2;").arg(kCardBg, kTextPrimary));
    setMinimumWidth(760);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    auto *desc = new QLabel(
        "채널별 ArUco 좌표 보정 상태입니다. 좌표 설정 자체는 라즈베리파이에서 SSH로 작업합니다 —\n"
        "여기서는 진행 상태 확인과, 새로 계산된 보정 파일을 서버 재시작 없이 다시 불러오는 것만 합니다.",
        this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    root->addWidget(desc);

    auto *topRow = new QHBoxLayout;
    refreshButton = new QPushButton("새로고침", this);
    refreshButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:white; border:none; border-radius:6px; padding:6px 14px; }"
        "QPushButton:hover { background-color:#7c6ce8; }").arg(kAccent));
    connect(refreshButton, &QPushButton::clicked, this, &ArucoCalibrationDialog::onRefreshClicked);
    topRow->addWidget(refreshButton);
    topRow->addStretch();
    root->addLayout(topRow);

    table = new QTableWidget(4, 8, this);
    table->setHorizontalHeaderLabels({"채널", "단계", "안내", "마커(검출/반영)", "오차(px)",
                                       "렌즈보정", "Homography", ""});
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(kColHint, QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->verticalHeader()->setVisible(false);
    table->setMinimumHeight(220);
    for (int row = 0; row < 4; row++)
        table->setItem(row, kColChannel, new QTableWidgetItem(QString("Ch.%1").arg(row + 1)));
    root->addWidget(table);

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

        table->setItem(row, kColChannel, new QTableWidgetItem(QString("Ch.%1").arg(s.channel)));
        auto *stageItem = new QTableWidgetItem(s.stage);
        if (s.ready)
            stageItem->setForeground(QColor("#34d399"));
        table->setItem(row, kColStage, stageItem);
        table->setItem(row, kColHint, new QTableWidgetItem(s.hint));

        if (s.hasLive) {
            table->setItem(row, kColMarkers,
                new QTableWidgetItem(QString("%1 / %2").arg(s.detectedMarkers).arg(s.acceptedMarkers)));
            table->setItem(row, kColError, new QTableWidgetItem(QString::number(s.errorPx, 'f', 2)));
            table->setItem(row, kColLens, new QTableWidgetItem(s.lensApplied ? "적용됨" : "-"));
            table->setItem(row, kColHomography, new QTableWidgetItem(s.staticHomography ? "있음" : "-"));
        } else {
            table->setItem(row, kColMarkers, new QTableWidgetItem("-"));
            table->setItem(row, kColError, new QTableWidgetItem("-"));
            table->setItem(row, kColLens, new QTableWidgetItem("-"));
            table->setItem(row, kColHomography, new QTableWidgetItem("-"));
        }

        auto *reloadBtn = new QPushButton("재로드", table);
        reloadBtn->setCursor(Qt::PointingHandCursor);
        reloadBtn->setStyleSheet(QString(
            "QPushButton { background-color:#232333; color:%1; border:1px solid %2; border-radius:6px; padding:4px 10px; }"
            "QPushButton:hover { border:1px solid %3; color:%3; }").arg(kTextPrimary, kCardBorder, kAccent));
        const int channel = s.channel;
        connect(reloadBtn, &QPushButton::clicked, this, [this, channel]() { onReloadClicked(channel); });
        table->setCellWidget(row, kColReload, reloadBtn);
    }
}
