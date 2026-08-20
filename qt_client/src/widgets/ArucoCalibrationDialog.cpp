#include "ArucoCalibrationDialog.h"
#include "../network/ServerLink.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QMessageBox>
#include <QJsonObject>
#include <QSet>
#include <algorithm>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";

constexpr int kMarkerIdCol = 0, kMarkerXCol = 1, kMarkerYCol = 2, kMarkerSizeCol = 3;

QDoubleSpinBox *makeCoordSpin(QWidget *parent, double value = 0) {
    auto *spin = new QDoubleSpinBox(parent);
    spin->setRange(-1000.0, 1000.0);
    spin->setDecimals(2);
    spin->setSuffix(" m");
    spin->setValue(value);
    return spin;
}
}

ArucoCalibrationDialog::ArucoCalibrationDialog(ServerLink *serverLink, QWidget *parent)
    : QDialog(parent), serverLink(serverLink)
{
    setWindowTitle("카메라 좌표 보정 (관리자)");
    setStyleSheet(QString("background-color:%1; color:%2;").arg(kCardBg, kTextPrimary));
    setMinimumWidth(720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    auto *desc = new QLabel(
        "채널을 고르고 공장 실제 좌표(미터)를 입력한 뒤 저장하면 서버가 검증·저장합니다.\n"
        "저장 후 \"보정 시작\"을 누르면 서버가 실제 카메라 영상으로 Homography를 계산합니다(수십 초 소요).",
        this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    root->addWidget(desc);

    // ── 채널 선택 + 불러오기 ──
    auto *channelRow = new QHBoxLayout;
    channelRow->addWidget(new QLabel("채널", this));
    channelCombo = new QComboBox(this);
    for (int ch = 1; ch <= 4; ch++)
        channelCombo->addItem(QString("Ch.%1").arg(ch), ch);
    connect(channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ArucoCalibrationDialog::onChannelChanged);
    channelRow->addWidget(channelCombo);

    loadButton = new QPushButton("현재 설정 불러오기", this);
    connect(loadButton, &QPushButton::clicked, this, &ArucoCalibrationDialog::onLoadClicked);
    channelRow->addWidget(loadButton);
    channelRow->addStretch();
    root->addLayout(channelRow);

    // ── 공장 전체 범위 + 축척 ──
    auto *factoryGroup = new QGroupBox("공장 전체 범위 · 모형 축척", this);
    factoryGroup->setStyleSheet(QString("QGroupBox { border:1px solid %1; border-radius:6px; margin-top:8px; padding-top:8px; } "
                                         "QGroupBox::title { subcontrol-origin: margin; left:8px; color:%2; }")
                                     .arg(kCardBorder, kTextSecondary));
    auto *factoryForm = new QFormLayout(factoryGroup);
    factoryMinXSpin = makeCoordSpin(this); factoryMinYSpin = makeCoordSpin(this);
    factoryMaxXSpin = makeCoordSpin(this, 60); factoryMaxYSpin = makeCoordSpin(this, 60);
    modelScaleSpin = new QDoubleSpinBox(this);
    modelScaleSpin->setRange(0.01, 10000.0);
    modelScaleSpin->setDecimals(2);
    modelScaleSpin->setValue(50);
    factoryForm->addRow("최소 X / 최소 Y", [&]{ auto *l = new QHBoxLayout; l->addWidget(factoryMinXSpin); l->addWidget(factoryMinYSpin); auto *w = new QWidget(this); w->setLayout(l); return w; }());
    factoryForm->addRow("최대 X / 최대 Y", [&]{ auto *l = new QHBoxLayout; l->addWidget(factoryMaxXSpin); l->addWidget(factoryMaxYSpin); auto *w = new QWidget(this); w->setLayout(l); return w; }());
    factoryForm->addRow("모형 축척 (1모형m = N공장m)", modelScaleSpin);
    root->addWidget(factoryGroup);

    // ── 채널 담당 범위 ──
    auto *boardGroup = new QGroupBox("이 채널 담당 범위", this);
    boardGroup->setStyleSheet(factoryGroup->styleSheet());
    auto *boardForm = new QFormLayout(boardGroup);
    boardMinXSpin = makeCoordSpin(this); boardMinYSpin = makeCoordSpin(this);
    boardMaxXSpin = makeCoordSpin(this); boardMaxYSpin = makeCoordSpin(this);
    boardForm->addRow("최소 X / 최소 Y", [&]{ auto *l = new QHBoxLayout; l->addWidget(boardMinXSpin); l->addWidget(boardMinYSpin); auto *w = new QWidget(this); w->setLayout(l); return w; }());
    boardForm->addRow("최대 X / 최대 Y", [&]{ auto *l = new QHBoxLayout; l->addWidget(boardMaxXSpin); l->addWidget(boardMaxYSpin); auto *w = new QWidget(this); w->setLayout(l); return w; }());
    root->addWidget(boardGroup);

    // ── 마커 ──
    auto *markerGroup = new QGroupBox("마커 (최소 4개)", this);
    markerGroup->setStyleSheet(factoryGroup->styleSheet());
    auto *markerLayout = new QVBoxLayout(markerGroup);

    auto *commonSizeRow = new QHBoxLayout;
    commonSizeRow->addWidget(new QLabel("공통 마커 크기", this));
    commonMarkerSizeSpin = new QDoubleSpinBox(this);
    commonMarkerSizeSpin->setRange(0.001, 5.0);
    commonMarkerSizeSpin->setDecimals(3);
    commonMarkerSizeSpin->setSuffix(" m");
    commonMarkerSizeSpin->setValue(0.04);
    commonSizeRow->addWidget(commonMarkerSizeSpin);
    auto *applyCommonSizeBtn = new QPushButton("전체 행에 적용", this);
    connect(applyCommonSizeBtn, &QPushButton::clicked, this, [this]() {
        for (int row = 0; row < markerTable->rowCount(); row++)
            markerTable->setItem(row, kMarkerSizeCol,
                new QTableWidgetItem(QString::number(commonMarkerSizeSpin->value(), 'f', 3)));
    });
    commonSizeRow->addWidget(applyCommonSizeBtn);
    commonSizeRow->addStretch();
    markerLayout->addLayout(commonSizeRow);

    markerTable = new QTableWidget(0, 4, this);
    markerTable->setHorizontalHeaderLabels({"마커 ID (0~49)", "X (m)", "Y (m)", "크기 (m)"});
    markerTable->horizontalHeader()->setStretchLastSection(true);
    markerTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    markerTable->setMinimumHeight(160);
    markerLayout->addWidget(markerTable);

    auto *markerBtnRow = new QHBoxLayout;
    auto *addRowBtn = new QPushButton("마커 추가", this);
    connect(addRowBtn, &QPushButton::clicked, this, &ArucoCalibrationDialog::onAddMarkerRow);
    markerBtnRow->addWidget(addRowBtn);
    auto *removeRowBtn = new QPushButton("선택 행 삭제", this);
    connect(removeRowBtn, &QPushButton::clicked, this, &ArucoCalibrationDialog::onRemoveSelectedMarkerRow);
    markerBtnRow->addWidget(removeRowBtn);
    markerBtnRow->addStretch();
    markerLayout->addLayout(markerBtnRow);
    root->addWidget(markerGroup);

    // ── 저장 / 보정 시작 ──
    auto *actionRow = new QHBoxLayout;
    saveButton = new QPushButton("저장", this);
    saveButton->setStyleSheet(QString("QPushButton { background-color:%1; color:white; border:none; border-radius:6px; padding:8px 16px; }").arg(kAccent));
    connect(saveButton, &QPushButton::clicked, this, &ArucoCalibrationDialog::onSaveClicked);
    actionRow->addWidget(saveButton);

    calibrateButton = new QPushButton("보정 시작", this);
    connect(calibrateButton, &QPushButton::clicked, this, &ArucoCalibrationDialog::onCalibrateClicked);
    actionRow->addWidget(calibrateButton);
    actionRow->addStretch();
    root->addLayout(actionRow);

    statusLabel = new QLabel("", this);
    statusLabel->setWordWrap(true);
    statusLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    root->addWidget(statusLabel);

    // ── 채널별 보정 상태 표 ──
    auto *statusGroup = new QGroupBox("채널별 보정 상태", this);
    statusGroup->setStyleSheet(factoryGroup->styleSheet());
    auto *statusLayout = new QVBoxLayout(statusGroup);
    channelStatusTable = new QTableWidget(4, 5, this);
    channelStatusTable->setHorizontalHeaderLabels({"채널", "설정됨", "마커 수", "Homography", "마지막 보정"});
    channelStatusTable->horizontalHeader()->setStretchLastSection(true);
    channelStatusTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelStatusTable->verticalHeader()->setVisible(false);
    statusLayout->addWidget(channelStatusTable);
    root->addWidget(statusGroup);

    connect(serverLink, &ServerLink::arucoConfigReceived, this, &ArucoCalibrationDialog::onArucoConfigReceived);
    connect(serverLink, &ServerLink::arucoConfigAck, this, &ArucoCalibrationDialog::onArucoConfigAck);
    connect(serverLink, &ServerLink::arucoStatusReceived, this, &ArucoCalibrationDialog::onArucoStatusReceived);
    connect(serverLink, &ServerLink::arucoCalibrationResult, this, &ArucoCalibrationDialog::onArucoCalibrationResult);
    connect(serverLink, &ServerLink::arucoCalibrationTimedOut, this, &ArucoCalibrationDialog::onArucoCalibrationTimedOut);

    onLoadClicked();
    serverLink->sendQuery("aruco_status", QJsonObject());
}

int ArucoCalibrationDialog::currentChannel() const
{
    return channelCombo->currentData().toInt();
}

void ArucoCalibrationDialog::onChannelChanged(int)
{
    onLoadClicked();
}

void ArucoCalibrationDialog::onLoadClicked()
{
    QJsonObject params;
    params["channel"] = currentChannel();
    serverLink->sendQuery("aruco_config", params);
    statusLabel->setText("불러오는 중...");
}

void ArucoCalibrationDialog::applyConfigToForm(const ArucoChannelConfig &config)
{
    factoryMinXSpin->setValue(config.factoryMinX);
    factoryMinYSpin->setValue(config.factoryMinY);
    factoryMaxXSpin->setValue(config.factoryMaxX > 0 ? config.factoryMaxX : 60);
    factoryMaxYSpin->setValue(config.factoryMaxY > 0 ? config.factoryMaxY : 60);
    modelScaleSpin->setValue(config.modelScale > 0 ? config.modelScale : 50);
    boardMinXSpin->setValue(config.boardMinX);
    boardMinYSpin->setValue(config.boardMinY);
    boardMaxXSpin->setValue(config.boardMaxX);
    boardMaxYSpin->setValue(config.boardMaxY);

    markerTable->setRowCount(0);
    for (const ArucoMarkerInput &m : config.markers) {
        const int row = markerTable->rowCount();
        markerTable->insertRow(row);
        markerTable->setItem(row, kMarkerIdCol, new QTableWidgetItem(QString::number(m.id)));
        markerTable->setItem(row, kMarkerXCol, new QTableWidgetItem(QString::number(m.x, 'f', 3)));
        markerTable->setItem(row, kMarkerYCol, new QTableWidgetItem(QString::number(m.y, 'f', 3)));
        markerTable->setItem(row, kMarkerSizeCol, new QTableWidgetItem(QString::number(m.sizeM, 'f', 3)));
    }

    statusLabel->setText(config.configured
        ? QString("Ch.%1 저장된 설정을 불러왔습니다 (마커 %2개).").arg(currentChannel()).arg(config.markers.size())
        : QString("Ch.%1은 아직 설정된 적 없습니다. 값을 입력하고 저장하세요.").arg(currentChannel()));
}

ArucoChannelConfig ArucoCalibrationDialog::collectConfigFromForm() const
{
    ArucoChannelConfig cfg;
    cfg.factoryMinX = factoryMinXSpin->value();
    cfg.factoryMinY = factoryMinYSpin->value();
    cfg.factoryMaxX = factoryMaxXSpin->value();
    cfg.factoryMaxY = factoryMaxYSpin->value();
    cfg.modelScale = modelScaleSpin->value();
    cfg.boardMinX = boardMinXSpin->value();
    cfg.boardMinY = boardMinYSpin->value();
    cfg.boardMaxX = boardMaxXSpin->value();
    cfg.boardMaxY = boardMaxYSpin->value();

    for (int row = 0; row < markerTable->rowCount(); row++) {
        auto textAt = [this, row](int col) {
            QTableWidgetItem *item = markerTable->item(row, col);
            return item ? item->text() : QString();
        };
        ArucoMarkerInput m;
        m.id = textAt(kMarkerIdCol).toInt();
        m.x = textAt(kMarkerXCol).toDouble();
        m.y = textAt(kMarkerYCol).toDouble();
        m.sizeM = textAt(kMarkerSizeCol).toDouble();
        cfg.markers.append(m);
    }
    return cfg;
}

void ArucoCalibrationDialog::onAddMarkerRow()
{
    const int row = markerTable->rowCount();
    markerTable->insertRow(row);
    markerTable->setItem(row, kMarkerIdCol, new QTableWidgetItem(QString::number(row)));
    markerTable->setItem(row, kMarkerXCol, new QTableWidgetItem("0.000"));
    markerTable->setItem(row, kMarkerYCol, new QTableWidgetItem("0.000"));
    markerTable->setItem(row, kMarkerSizeCol,
        new QTableWidgetItem(QString::number(commonMarkerSizeSpin->value(), 'f', 3)));
}

void ArucoCalibrationDialog::onRemoveSelectedMarkerRow()
{
    const QList<QTableWidgetItem *> selected = markerTable->selectedItems();
    if (selected.isEmpty())
        return;
    // 여러 셀이 한 행에서 선택될 수 있어 행 번호를 모아 중복 없이 큰 것부터 지운다.
    QSet<int> rows;
    for (QTableWidgetItem *item : selected)
        rows.insert(item->row());
    QList<int> sorted = rows.values();
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : sorted)
        markerTable->removeRow(row);
}

void ArucoCalibrationDialog::onSaveClicked()
{
    const ArucoChannelConfig cfg = collectConfigFromForm();
    if (cfg.markers.size() < 4) {
        QMessageBox::warning(this, "마커 부족", "마커는 채널당 최소 4개 필요합니다.");
        return;
    }
    saveButton->setEnabled(false);
    statusLabel->setText("저장 중...");
    pendingSaveCmdId = serverLink->sendSetArucoConfig(currentChannel(), cfg);
}

void ArucoCalibrationDialog::onArucoConfigAck(const QString &cmdId, int channel, bool ok, const QString &reason)
{
    if (cmdId != pendingSaveCmdId)
        return;
    pendingSaveCmdId.clear();
    saveButton->setEnabled(true);
    if (ok) {
        statusLabel->setText(QString("완료: Ch.%1 설정 저장됨. 기존 Homography는 무효화됐습니다 — \"보정 시작\"으로 새로 계산하세요.").arg(channel));
        serverLink->sendQuery("aruco_status", QJsonObject());
    } else {
        statusLabel->setText(QString("실패: %1").arg(reason.isEmpty() ? "알 수 없는 오류" : reason));
    }
}

void ArucoCalibrationDialog::onCalibrateClicked()
{
    setBusy(true, QString("Ch.%1 보정 중... (실제 카메라 영상으로 계산, 수십 초 소요)").arg(currentChannel()));
    pendingCalibrationCmdId = serverLink->sendStartArucoCalibration(currentChannel());
}

void ArucoCalibrationDialog::setBusy(bool busy, const QString &statusText)
{
    saveButton->setEnabled(!busy);
    calibrateButton->setEnabled(!busy);
    loadButton->setEnabled(!busy);
    statusLabel->setText(statusText);
}

void ArucoCalibrationDialog::onArucoCalibrationResult(const QString &cmdId, int channel, bool ok,
                                                        const QString &reason, int acceptedMarkers,
                                                        int detectedMarkers, double rmsPx)
{
    if (cmdId != pendingCalibrationCmdId)
        return;
    pendingCalibrationCmdId.clear();
    setBusy(false, ok
        ? QString("완료: Ch.%1 보정 성공 (검출 마커 %2개, 반영 %3개, 오차 %4px)")
              .arg(channel).arg(detectedMarkers).arg(acceptedMarkers).arg(rmsPx, 0, 'f', 2)
        : QString("실패: Ch.%1 보정 실패 — %2").arg(channel).arg(reason.isEmpty() ? "알 수 없는 오류" : reason));
    serverLink->sendQuery("aruco_status", QJsonObject());
}

void ArucoCalibrationDialog::onArucoCalibrationTimedOut(const QString &cmdId, int channel)
{
    if (cmdId != pendingCalibrationCmdId)
        return;
    pendingCalibrationCmdId.clear();
    setBusy(false, QString("응답 없음 — Ch.%1 서버 연결을 확인해주세요.").arg(channel));
}

void ArucoCalibrationDialog::onArucoConfigReceived(int channel, const ArucoChannelConfig &config)
{
    if (channel != currentChannel())
        return;
    applyConfigToForm(config);
}

void ArucoCalibrationDialog::onArucoStatusReceived(const QVector<ArucoChannelStatus> &channels)
{
    for (const ArucoChannelStatus &s : channels) {
        if (s.channel < 1 || s.channel > 4)
            continue;
        const int row = s.channel - 1;
        channelStatusTable->setItem(row, 0, new QTableWidgetItem(QString("Ch.%1").arg(s.channel)));
        channelStatusTable->setItem(row, 1, new QTableWidgetItem(s.configured ? "설정됨" : "미설정"));
        channelStatusTable->setItem(row, 2, new QTableWidgetItem(QString::number(s.markerCount)));
        channelStatusTable->setItem(row, 3, new QTableWidgetItem(s.homographyExists ? "있음" : "없음"));

        QString lastText = "-";
        if (s.calibrating)
            lastText = "보정 중...";
        else if (s.hasLastResult)
            lastText = s.lastOk
                ? QString("성공 (검출 %1개, 오차 %2px)").arg(s.lastDetectedMarkers).arg(s.lastRmsPx, 0, 'f', 2)
                : QString("실패 — %1").arg(s.lastReason);
        channelStatusTable->setItem(row, 4, new QTableWidgetItem(lastText));
    }
}
