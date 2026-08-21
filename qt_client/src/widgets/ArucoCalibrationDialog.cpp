#include "ArucoCalibrationDialog.h"
#include "ArucoConfigDialog.h"
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
              kColError = 4, kColLens = 5, kColHomography = 6, kColActions = 7;

// 재로드/실행/중단은 완료 응답이 없어 몇 초 뒤 자동으로 다시 조회해서 반영한다(워커가 "다음
// 프레임"에 처리하므로 즉시 조회하면 아직 반영 전일 수 있음).
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
    setWindowTitle("카메라 좌표 보정 (관리자)");
    setStyleSheet(QString("background-color:%1; color:%2;").arg(kCardBg, kTextPrimary));
    setMinimumWidth(1300);
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
        "채널별 ArUco 좌표 보정입니다. \"설정\"에서 마커 좌표를 입력·저장하고, \"실행\"으로 서버가 "
        "실제로 마커를 검출해 보정을 계산하게 합니다(수 초~90초, 최대 180초 뒤 자동 중단). "
        "\"재로드\"는 이미 계산된 보정 파일을 서버 재시작 없이 다시 적용합니다.",
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
                                       "렌즈보정", "Homography", "동작"});
    table->setStyleSheet(tableStyleSheet());
    table->setShowGrid(true);
    table->setFrameShape(QFrame::NoFrame);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(52);
    table->horizontalHeader()->setHighlightSections(false);
    table->horizontalHeader()->setMinimumSectionSize(50);
    table->setWordWrap(true);
    // "안내"(kColHint)만 Stretch로 남기고 나머지는 실제 들어갈 값 중 제일 긴 것 기준으로
    // 여유 있게 고정폭을 준다 — 이전엔 다 너무 빡빡하게 잡아서 헤더/본문이 잘렸었음.
    // (가장 긴 값들: 단계="렌즈 보정값 없음", 마커헤더="검출/반영 마커", Homography=영문 10자)
    table->setColumnWidth(kColChannel, 60);
    table->setColumnWidth(kColStage, 150);
    table->setColumnWidth(kColMarkers, 140);
    table->setColumnWidth(kColError, 90);
    table->setColumnWidth(kColLens, 90);
    table->setColumnWidth(kColHomography, 120);
    table->setColumnWidth(kColActions, 260); // 설정/실행·중단/재로드 버튼 3개가 한 줄에 들어가야 함
    table->horizontalHeader()->setSectionResizeMode(kColChannel, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColStage, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColHint, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(kColMarkers, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColError, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColLens, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColHomography, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(kColActions, QHeaderView::Fixed);
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
    connect(serverLink, &ServerLink::runCalibrationResult, this, &ArucoCalibrationDialog::onRunCalibrationResult);
    connect(serverLink, &ServerLink::cancelCalibrationResult, this, &ArucoCalibrationDialog::onCancelCalibrationResult);
    connect(serverLink, &ServerLink::calibrationDone, this, &ArucoCalibrationDialog::onCalibrationDone);

    requestStatus();
}

void ArucoCalibrationDialog::requestStatus()
{
    serverLink->sendQuery("calib_status", QJsonObject());
    // calibration_done 등에서 색을 입혀둔 채로 남아있으면(초록/빨강) 다음 메시지까지 잘못 물들어
    // 보이므로, 새로 조회할 때마다 기본 색으로 되돌린다.
    statusLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
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

        auto *stageItem = new QTableWidgetItem(s.stage + (s.running ? " (보정 중)" : ""));
        QFont stageFont = stageItem->font();
        stageFont.setBold(true);
        stageItem->setFont(stageFont);
        stageItem->setForeground(QColor(s.running ? kAccent : (s.ready ? "#34d399" : "#fbbf24")));
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

        const int channel = s.channel;
        const QString actionBtnStyle = QString(
            "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:5px 8px; }"
            "QPushButton:hover { border:1px solid %4; color:%4; }").arg(kBg, kTextPrimary, kCardBorder, kAccent);

        auto *configBtn = new QPushButton("설정", table);
        configBtn->setCursor(Qt::PointingHandCursor);
        configBtn->setStyleSheet(actionBtnStyle);
        connect(configBtn, &QPushButton::clicked, this, [this, channel]() { onConfigClicked(channel); });

        // 실행/중단은 running 상태에 따라 하나의 자리에서 바뀐다 — 실행 중일 땐 다시 실행할 수
        // 없게 막고(서버가 어차피 거부하겠지만, 누를 필요가 없다는 걸 눈으로도 보여준다) 중단만 가능.
        QPushButton *runOrCancelBtn;
        if (s.running) {
            runOrCancelBtn = new QPushButton("■ 중단", table);
            runOrCancelBtn->setStyleSheet(QString(
                "QPushButton { background-color:%1; color:#f87171; border:1px solid %2; border-radius:6px; padding:5px 8px; }"
                "QPushButton:hover { border:1px solid #f87171; }").arg(kBg, kCardBorder));
            connect(runOrCancelBtn, &QPushButton::clicked, this, [this, channel]() { onCancelClicked(channel); });
        } else {
            runOrCancelBtn = new QPushButton("▶ 실행", table);
            runOrCancelBtn->setStyleSheet(actionBtnStyle);
            connect(runOrCancelBtn, &QPushButton::clicked, this, [this, channel]() { onRunClicked(channel); });
        }
        runOrCancelBtn->setCursor(Qt::PointingHandCursor);

        auto *reloadBtn = new QPushButton("재로드", table);
        reloadBtn->setCursor(Qt::PointingHandCursor);
        reloadBtn->setStyleSheet(actionBtnStyle);
        connect(reloadBtn, &QPushButton::clicked, this, [this, channel]() { onReloadClicked(channel); });

        auto *cellWrap = new QWidget(table);
        auto *cellLayout = new QHBoxLayout(cellWrap);
        cellLayout->setContentsMargins(6, 4, 6, 4);
        cellLayout->setSpacing(4);
        cellLayout->addWidget(configBtn);
        cellLayout->addWidget(runOrCancelBtn);
        cellLayout->addWidget(reloadBtn);
        table->setCellWidget(row, kColActions, cellWrap);
    }
}

void ArucoCalibrationDialog::onConfigClicked(int channel)
{
    // exec()로 모달 — 저장(accept)되면 그 채널 마커 구성이 바뀐 것이니 상태를 다시 조회해서
    // "단계/안내" 문구가 최신인지 확인한다(예: "미설정" -> "보정 미완료"로 바뀌었을 수 있음).
    auto *dialog = new ArucoConfigDialog(serverLink, channel, this);
    if (dialog->exec() == QDialog::Accepted)
        requestStatus();
    dialog->deleteLater();
}

void ArucoCalibrationDialog::onRunClicked(int channel)
{
    statusLabel->setText(QString("Ch.%1 보정 실행 요청 중...").arg(channel));
    serverLink->sendRunCalibration(channel);
}

void ArucoCalibrationDialog::onCancelClicked(int channel)
{
    statusLabel->setText(QString("Ch.%1 보정 중단 요청 중...").arg(channel));
    serverLink->sendCancelCalibration(channel);
}

void ArucoCalibrationDialog::onRunCalibrationResult(int channel, bool accepted, const QString &reason)
{
    if (!accepted) {
        statusLabel->setText(QString("실패: Ch.%1 보정 실행 요청 거부 — %2")
                                  .arg(channel).arg(reason.isEmpty() ? "알 수 없는 사유" : reason));
        return;
    }
    statusLabel->setText(QString("접수됨: Ch.%1 보정 실행 — 완료까지 수 초~90초 걸립니다 "
                                  "(최대 180초 뒤 자동 중단). 진행 상태를 반영합니다.").arg(channel));
    // 접수 직후 바로 조회하면 running=true를 빨리 반영해서 버튼이 "중단"으로 즉시 바뀐다.
    requestStatus();
}

void ArucoCalibrationDialog::onCancelCalibrationResult(int channel, bool accepted, const QString &reason)
{
    if (!accepted) {
        statusLabel->setText(QString("실패: Ch.%1 보정 중단 요청 거부 — %2")
                                  .arg(channel).arg(reason.isEmpty() ? "알 수 없는 사유" : reason));
        return;
    }
    statusLabel->setText(QString("접수됨: Ch.%1 보정 중단 — 곧 calibration_done(cancelled)이 옵니다.").arg(channel));
}

void ArucoCalibrationDialog::onCalibrationDone(int channel, const QString &result)
{
    // cancelled는 사용자가 직접 누른 것 — 오류 취급하지 않는다(대홍님 회신 그대로).
    if (result == "ok") {
        statusLabel->setStyleSheet("color:#34d399; font-size:13px;");
        statusLabel->setText(QString("Ch.%1 보정 완료 — 보정 파일이 자동으로 적용됐습니다.").arg(channel));
    } else if (result == "cancelled") {
        statusLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
        statusLabel->setText(QString("Ch.%1 보정이 중단됐습니다.").arg(channel));
    } else if (result == "timeout") {
        statusLabel->setStyleSheet("color:#f87171; font-size:13px;");
        statusLabel->setText(QString("Ch.%1 보정이 180초를 넘겨 자동 중단됐습니다. 마커가 카메라에 "
                                      "잘 보이는지 확인 후 다시 시도해주세요.").arg(channel));
    } else {
        statusLabel->setStyleSheet("color:#f87171; font-size:13px;");
        statusLabel->setText(QString("Ch.%1 보정 실패 (%2).").arg(channel).arg(result));
    }
    // 바로 requestStatus()를 부르면 이 메시지가 "불러오는 중..."으로 즉시 덮여서 안 보인다 —
    // 잠깐 읽을 시간을 준 뒤(재로드와 같은 지연) running=false로 바뀐 최신 상태를 다시 조회한다.
    autoRefreshTimer->start(kAutoRefreshDelayMs);
}
