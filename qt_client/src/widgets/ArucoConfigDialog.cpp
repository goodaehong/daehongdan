#include "ArucoConfigDialog.h"
#include "../network/ServerLink.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QAbstractSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QSet>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";

constexpr int kColId = 0, kColX = 1, kColY = 2, kColSize = 3, kColRotation = 4, kColDelete = 5;

QString spinBoxStyle()
{
    // 네이티브 up/down 화살표는 이 스타일(Qt 기본 렌더링도, QSS 테두리로 직접 그리는 것도)에서
    // 안 보이거나 뭉개져서 신뢰할 수 없었음 — 그래서 네이티브 버튼은 아예 끄고(NoButtons,
    // withSteppers 참고) ▲▼ 글자를 직접 그리는 별도 버튼으로 대체했다. 왼쪽만 둥글게 해서
    // 오른쪽에 붙는 버튼 칼럼과 이어지는 모양을 만든다.
    return QString(
        "QDoubleSpinBox, QSpinBox { background-color:#1a1a26; color:%1; border:1px solid %2; "
        "border-top-left-radius:6px; border-bottom-left-radius:6px; border-top-right-radius:0; "
        "border-bottom-right-radius:0; padding:6px 8px; min-height:24px; }"
        "QDoubleSpinBox:focus, QSpinBox:focus { border:1px solid %3; }")
        .arg(kTextPrimary, kCardBorder, kAccent);
}

QString stepButtonStyle(bool isTop)
{
    return QString(
        "QPushButton { background-color:#2a2a3d; color:%1; border:1px solid %2; "
        "border-left:none; %3 font-size:9px; padding:0; }"
        "QPushButton:hover { background-color:%4; }"
        "QPushButton:pressed { background-color:#6d5fd1; }")
        .arg(kTextPrimary, kCardBorder,
             isTop ? "border-bottom:none; border-top-right-radius:6px;"
                   : "border-bottom-right-radius:6px;",
             kAccent);
}

// 스핀박스 오른쪽에 ▲/▼ 버튼 두 개를 붙인 위젯을 만들어 반환한다(레이아웃에는 이걸 넣어야 함).
// 네이티브 위아래 버튼은 생성자에서 이미 꺼둔 상태로 넘어온다.
QWidget *withSteppers(QAbstractSpinBox *sb)
{
    auto *upBtn = new QPushButton(QString(QChar(0x25B2)));
    auto *downBtn = new QPushButton(QString(QChar(0x25BC)));
    upBtn->setCursor(Qt::PointingHandCursor);
    downBtn->setCursor(Qt::PointingHandCursor);
    upBtn->setFixedSize(22, 18);
    downBtn->setFixedSize(22, 18);
    upBtn->setStyleSheet(stepButtonStyle(true));
    downBtn->setStyleSheet(stepButtonStyle(false));
    QObject::connect(upBtn, &QPushButton::clicked, sb, [sb]() { sb->stepUp(); });
    QObject::connect(downBtn, &QPushButton::clicked, sb, [sb]() { sb->stepDown(); });

    auto *btnCol = new QVBoxLayout;
    btnCol->setSpacing(0);
    btnCol->setContentsMargins(0, 0, 0, 0);
    btnCol->addWidget(upBtn);
    btnCol->addWidget(downBtn);

    auto *row = new QWidget(sb->parentWidget());
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(sb, 1);
    rowLayout->addLayout(btnCol);
    return row;
}

QDoubleSpinBox *makeRangeSpinBox(QWidget *parent)
{
    auto *sb = new QDoubleSpinBox(parent);
    sb->setRange(-9999.0, 9999.0);
    sb->setDecimals(2);
    sb->setSingleStep(0.5);
    sb->setMinimumHeight(38);
    sb->setButtonSymbols(QAbstractSpinBox::NoButtons);
    sb->setStyleSheet(spinBoxStyle());
    return sb;
}
}

ArucoConfigDialog::ArucoConfigDialog(ServerLink *serverLink, int channel, QWidget *parent)
    : QDialog(parent), serverLink(serverLink), channel(channel)
{
    setWindowTitle(QString("Ch.%1 ArUco 좌표 설정").arg(channel));
    setStyleSheet(QString("background-color:%1; color:%2;").arg(kCardBg, kTextPrimary));
    setMinimumWidth(640);
    setMinimumHeight(560);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(14);

    auto *desc = new QLabel(
        "이 채널 카메라가 담당하는 공장 실측 범위, 모형 축척, 그리고 실제로 배치한 ArUco 마커 "
        "좌표를 입력합니다. 마커는 4개 이상, ID는 0~49 안에서 서로 겹치지 않아야 하고, 채널 담당 "
        "범위 안에 있어야 합니다.", this);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    root->addWidget(desc);

    QString groupStyle = QString(
        "QGroupBox { border:1px solid %1; border-radius:8px; margin-top:10px; padding:14px 10px 10px 10px; "
        "font-weight:bold; color:%2; }"
        "QGroupBox::title { subcontrol-origin:margin; left:10px; padding:0 4px; }")
        .arg(kCardBorder, kTextPrimary);

    // 공장 범위 + 모형 축척
    auto *factoryGroup = new QGroupBox("공장 범위 (m) · 모형 축척", this);
    factoryGroup->setStyleSheet(groupStyle);
    auto *factoryGrid = new QGridLayout(factoryGroup);
    factoryMinX = makeRangeSpinBox(factoryGroup);
    factoryMinY = makeRangeSpinBox(factoryGroup);
    factoryMaxX = makeRangeSpinBox(factoryGroup);
    factoryMaxY = makeRangeSpinBox(factoryGroup);
    modelScale = makeRangeSpinBox(factoryGroup);
    modelScale->setRange(0.01, 9999.0);
    auto lbl = [this](const QString &t) {
        auto *l = new QLabel(t, this);
        l->setStyleSheet(QString("color:%1; border:none;").arg(kTextSecondary));
        return l;
    };
    factoryGrid->addWidget(lbl("최소 X"), 0, 0); factoryGrid->addWidget(withSteppers(factoryMinX), 0, 1);
    factoryGrid->addWidget(lbl("최소 Y"), 0, 2); factoryGrid->addWidget(withSteppers(factoryMinY), 0, 3);
    factoryGrid->addWidget(lbl("최대 X"), 1, 0); factoryGrid->addWidget(withSteppers(factoryMaxX), 1, 1);
    factoryGrid->addWidget(lbl("최대 Y"), 1, 2); factoryGrid->addWidget(withSteppers(factoryMaxY), 1, 3);
    factoryGrid->addWidget(lbl("모형 축척"), 2, 0); factoryGrid->addWidget(withSteppers(modelScale), 2, 1);
    root->addWidget(factoryGroup);

    // 채널 담당(보드) 범위
    auto *boardGroup = new QGroupBox("채널 담당 범위 (m)", this);
    boardGroup->setStyleSheet(groupStyle);
    auto *boardGrid = new QGridLayout(boardGroup);
    boardMinX = makeRangeSpinBox(boardGroup);
    boardMinY = makeRangeSpinBox(boardGroup);
    boardMaxX = makeRangeSpinBox(boardGroup);
    boardMaxY = makeRangeSpinBox(boardGroup);
    boardGrid->addWidget(lbl("최소 X"), 0, 0); boardGrid->addWidget(withSteppers(boardMinX), 0, 1);
    boardGrid->addWidget(lbl("최소 Y"), 0, 2); boardGrid->addWidget(withSteppers(boardMinY), 0, 3);
    boardGrid->addWidget(lbl("최대 X"), 1, 0); boardGrid->addWidget(withSteppers(boardMaxX), 1, 1);
    boardGrid->addWidget(lbl("최대 Y"), 1, 2); boardGrid->addWidget(withSteppers(boardMaxY), 1, 3);
    root->addWidget(boardGroup);

    // 마커 테이블
    auto *markerRow = new QHBoxLayout;
    auto *markerTitle = new QLabel("마커 배치", this);
    markerTitle->setStyleSheet(QString("color:%1; font-weight:bold;").arg(kTextPrimary));
    markerRow->addWidget(markerTitle);
    markerRow->addStretch();
    auto *addMarkerBtn = new QPushButton("+ 마커 추가", this);
    addMarkerBtn->setCursor(Qt::PointingHandCursor);
    addMarkerBtn->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:6px 12px; }"
        "QPushButton:hover { border:1px solid %4; color:%4; }").arg(kCardBg, kTextSecondary, kCardBorder, kAccent));
    connect(addMarkerBtn, &QPushButton::clicked, this, &ArucoConfigDialog::onAddMarkerRow);
    markerRow->addWidget(addMarkerBtn);
    root->addLayout(markerRow);

    markerTable = new QTableWidget(0, 6, this);
    markerTable->setHorizontalHeaderLabels({"ID", "X(m)", "Y(m)", "크기(cm)", "회전(도)", ""});
    markerTable->setStyleSheet(QString(
        "QTableWidget { background-color:%1; color:%2; border:1px solid %3; border-radius:8px; "
        "gridline-color:%3; font-size:13px; }"
        "QHeaderView::section { background-color:#0d0d16; color:%4; border:none; "
        "border-bottom:1px solid %3; padding:6px; }").arg(kCardBg, kTextPrimary, kCardBorder, kTextSecondary));
    markerTable->verticalHeader()->setVisible(false);
    markerTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    markerTable->horizontalHeader()->setSectionResizeMode(kColDelete, QHeaderView::Fixed);
    markerTable->setColumnWidth(kColDelete, 40);
    markerTable->setMinimumHeight(180);
    root->addWidget(markerTable, 1);

    statusLabel = new QLabel("불러오는 중...", this);
    statusLabel->setWordWrap(true);
    statusLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    root->addWidget(statusLabel);

    auto *btnRow = new QHBoxLayout;
    auto *cancelBtn = new QPushButton("취소", this);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:8px; padding:10px; }"
        "QPushButton:hover { border:1px solid %4; color:%4; }").arg(kCardBg, kTextSecondary, kCardBorder, kAccent));
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    saveButton = new QPushButton("저장", this);
    saveButton->setCursor(Qt::PointingHandCursor);
    saveButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:white; font-weight:bold; border:none; border-radius:8px; padding:10px; }"
        "QPushButton:hover { background-color:#7c6ce8; }").arg(kAccent));
    connect(saveButton, &QPushButton::clicked, this, &ArucoConfigDialog::onSaveClicked);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveButton);
    root->addLayout(btnRow);

    connect(serverLink, &ServerLink::arucoConfigReceived, this, &ArucoConfigDialog::onArucoConfigReceived);
    connect(serverLink, &ServerLink::arucoConfigResult, this, &ArucoConfigDialog::onArucoConfigResult);

    pendingQueryReqId = serverLink->sendQueryArucoConfig(channel);
}

void ArucoConfigDialog::addMarkerRow(int id, double x, double y, double sizeCm, double rotation)
{
    const int row = markerTable->rowCount();
    markerTable->insertRow(row);

    auto *idBox = new QSpinBox(markerTable);
    idBox->setRange(0, 49);
    idBox->setValue(id);
    idBox->setMinimumHeight(38);
    idBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    idBox->setStyleSheet(spinBoxStyle());
    markerTable->setCellWidget(row, kColId, withSteppers(idBox));

    auto *xBox = makeRangeSpinBox(markerTable);
    xBox->setValue(x);
    markerTable->setCellWidget(row, kColX, withSteppers(xBox));

    auto *yBox = makeRangeSpinBox(markerTable);
    yBox->setValue(y);
    markerTable->setCellWidget(row, kColY, withSteppers(yBox));

    auto *sizeBox = makeRangeSpinBox(markerTable);
    sizeBox->setRange(0.1, 999.0);
    sizeBox->setValue(sizeCm > 0 ? sizeCm : 5.0);
    markerTable->setCellWidget(row, kColSize, withSteppers(sizeBox));

    auto *rotBox = makeRangeSpinBox(markerTable);
    rotBox->setRange(-360.0, 360.0);
    rotBox->setValue(rotation);
    markerTable->setCellWidget(row, kColRotation, withSteppers(rotBox));

    auto *delBtn = new QPushButton("×", markerTable);
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setStyleSheet("QPushButton { background:transparent; color:#f87171; border:none; font-size:16px; font-weight:bold; }"
                          "QPushButton:hover { color:#fca5a5; }");
    connect(delBtn, &QPushButton::clicked, this, [this, delBtn]() {
        // 삭제 시점의 실제 행 번호를 다시 찾는다 — 다른 행이 먼저 지워지면 캡처해둔 row가 밀린다.
        for (int r = 0; r < markerTable->rowCount(); ++r) {
            if (markerTable->cellWidget(r, kColDelete) == delBtn) {
                markerTable->removeRow(r);
                break;
            }
        }
    });
    markerTable->setCellWidget(row, kColDelete, delBtn);
}

void ArucoConfigDialog::onAddMarkerRow()
{
    // 다음 자동 ID: 지금 안 쓰이는 가장 작은 값(0~49) — 매번 0부터 새로 채워 넣는 수고를 던다.
    QSet<int> used;
    for (int r = 0; r < markerTable->rowCount(); ++r) {
        // setCellWidget에 들어간 건 스핀박스를 감싼 위젯(withSteppers)이라 findChild로 찾는다.
        if (auto *idBox = markerTable->cellWidget(r, kColId)->findChild<QSpinBox *>())
            used.insert(idBox->value());
    }
    int nextId = 0;
    while (used.contains(nextId) && nextId <= 49) nextId++;
    addMarkerRow(qBound(0, nextId, 49), 0.0, 0.0, 5.0, 0.0);
}

void ArucoConfigDialog::fillForm(const ArucoChannelConfig &config)
{
    factoryMinX->setValue(config.factoryMinX);
    factoryMinY->setValue(config.factoryMinY);
    factoryMaxX->setValue(config.factoryMaxX);
    factoryMaxY->setValue(config.factoryMaxY);
    modelScale->setValue(config.modelScale > 0 ? config.modelScale : 1.0);
    boardMinX->setValue(config.boardMinX);
    boardMinY->setValue(config.boardMinY);
    boardMaxX->setValue(config.boardMaxX);
    boardMaxY->setValue(config.boardMaxY);

    markerTable->setRowCount(0);
    for (const ArucoMarkerConfig &m : config.markers)
        addMarkerRow(m.id, m.x, m.y, m.sizeCm, m.rotation);
}

void ArucoConfigDialog::onArucoConfigReceived(int ch, bool available, const ArucoChannelConfig &config)
{
    if (ch != channel)
        return; // 다른 채널 응답 — 이 다이얼로그와 무관
    if (available) {
        fillForm(config);
        statusLabel->setText("기존 설정을 불러왔습니다. 수정 후 저장하세요.");
    } else {
        statusLabel->setText("아직 설정된 좌표가 없습니다. 아래에 새로 입력하세요.");
    }
}

bool ArucoConfigDialog::collectConfig(ArucoChannelConfig &out, QString *errorOut) const
{
    out.channel = channel;
    out.factoryMinX = factoryMinX->value();
    out.factoryMinY = factoryMinY->value();
    out.factoryMaxX = factoryMaxX->value();
    out.factoryMaxY = factoryMaxY->value();
    out.modelScale = modelScale->value();
    out.boardMinX = boardMinX->value();
    out.boardMinY = boardMinY->value();
    out.boardMaxX = boardMaxX->value();
    out.boardMaxY = boardMaxY->value();

    out.markers.clear();
    QSet<int> seenIds;
    for (int r = 0; r < markerTable->rowCount(); ++r) {
        ArucoMarkerConfig m;
        // setCellWidget에 들어간 건 스핀박스를 감싼 위젯(withSteppers)이라 findChild로 찾는다.
        m.id = markerTable->cellWidget(r, kColId)->findChild<QSpinBox *>()->value();
        m.x = markerTable->cellWidget(r, kColX)->findChild<QDoubleSpinBox *>()->value();
        m.y = markerTable->cellWidget(r, kColY)->findChild<QDoubleSpinBox *>()->value();
        m.sizeCm = markerTable->cellWidget(r, kColSize)->findChild<QDoubleSpinBox *>()->value();
        m.rotation = markerTable->cellWidget(r, kColRotation)->findChild<QDoubleSpinBox *>()->value();

        if (seenIds.contains(m.id)) {
            if (errorOut) *errorOut = QString("마커 ID %1이(가) 중복됩니다.").arg(m.id);
            return false;
        }
        seenIds.insert(m.id);

        if (m.x < out.boardMinX || m.x > out.boardMaxX || m.y < out.boardMinY || m.y > out.boardMaxY) {
            if (errorOut) *errorOut = QString("마커 ID %1의 좌표가 채널 담당 범위를 벗어났습니다.").arg(m.id);
            return false;
        }
        out.markers.append(m);
    }

    if (out.markers.size() < 4) {
        if (errorOut) *errorOut = QString("마커는 최소 4개 필요합니다 (현재 %1개).").arg(out.markers.size());
        return false;
    }
    if (out.factoryMinX >= out.factoryMaxX || out.factoryMinY >= out.factoryMaxY) {
        if (errorOut) *errorOut = "공장 범위의 최소값이 최대값보다 크거나 같습니다.";
        return false;
    }
    if (out.boardMinX >= out.boardMaxX || out.boardMinY >= out.boardMaxY) {
        if (errorOut) *errorOut = "채널 담당 범위의 최소값이 최대값보다 크거나 같습니다.";
        return false;
    }
    return true;
}

void ArucoConfigDialog::onSaveClicked()
{
    ArucoChannelConfig config;
    QString error;
    if (!collectConfig(config, &error)) {
        statusLabel->setStyleSheet("color:#f87171; font-size:13px;");
        statusLabel->setText(error);
        return;
    }

    saveButton->setEnabled(false);
    statusLabel->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    statusLabel->setText("저장 중...");
    serverLink->sendSetArucoConfig(config);
}

void ArucoConfigDialog::onArucoConfigResult(int ch, bool ok, const QString &reason)
{
    if (ch != channel)
        return;
    if (ok) {
        accept(); // 저장 성공 — 다이얼로그 닫고 호출부(ArucoCalibrationDialog)가 상태를 다시 조회
        return;
    }
    saveButton->setEnabled(true);
    statusLabel->setStyleSheet("color:#f87171; font-size:13px;");
    statusLabel->setText(QString("저장 실패: %1").arg(reason.isEmpty() ? "알 수 없는 오류" : reason));
}
