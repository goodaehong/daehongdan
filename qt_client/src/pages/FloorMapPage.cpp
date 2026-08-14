#include "FloorMapPage.h"
#include "../widgets/FloorMapGridWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QFrame>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";

// 서버가 base64 전송 줄 길이를 8MB로 제한할 예정(대홍 회신) — base64는 원본의 약 1.33배이므로
// 원본 기준 6MB를 상한으로 둔다.
constexpr qint64 kMaxUploadBytes = 6LL * 1024 * 1024;

// 서버 연동(평면도 대피경로 연동 프로토콜 제안 문서) 전까지 UI 확인용으로만 쓰는 예시 데이터.
// 62x62 격자, 테두리+내부 벽 몇 개, 전광판 2개, 출구 2개, 손으로 짠 L자 경로.
constexpr int kGridSize = 62;

QVector<QVector<int>> samplePlaceholderBitmap()
{
    QVector<QVector<int>> bmp(kGridSize, QVector<int>(kGridSize, 0));
    for (int i = 0; i < kGridSize; ++i) {
        bmp[0][i] = bmp[kGridSize - 1][i] = 1;
        bmp[i][0] = bmp[i][kGridSize - 1] = 1;
    }
    // 출구 자리(경계선 위)는 뚫어둔다.
    bmp[0][30] = 0;
    bmp[61][15] = 0;
    // 내부 칸막이 몇 개 — 통행 가능 경로가 꺾이는 걸 보여주기 위한 용도.
    for (int y = 10; y < 40; ++y) bmp[y][25] = 1;
    bmp[39][25] = 0; // 통로
    for (int x = 35; x < 55; ++x) bmp[45][x] = 1;
    bmp[45][54] = 0; // 통로
    return bmp;
}
}

FloorMapPage::FloorMapPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);

    auto *headerRow = new QHBoxLayout;
    auto *title = new QLabel("평면도", this);
    title->setStyleSheet(QString("color:%1; font-size:20px; font-weight:bold; font-family:\"hanwhaGothic EL\";").arg(kTextPrimary));
    headerRow->addWidget(title);
    headerRow->addStretch();

    // 자주 안 쓰는 관리 작업(등록/재설정)이라 눈에 띄되 주 동선을 가리지 않는 자리에 둔다.
    resetButton = new QPushButton("지도 재설정", this);
    resetButton->setCursor(Qt::PointingHandCursor);
    resetButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:%2; border:1px solid %3; border-radius:6px; padding:8px 16px; }"
        "QPushButton:hover { border:1px solid %4; color:%4; }").arg(kCardBg, kTextSecondary, kCardBorder, kAccent));
    connect(resetButton, &QPushButton::clicked, this, &FloorMapPage::openSetupPanel);
    headerRow->addWidget(resetButton);
    layout->addLayout(headerRow);

    // 지도 미등록 상태를 탭 배지("❗ 평면도")뿐 아니라 탭에 들어오는 즉시도 알 수 있도록
    // 페이지 최상단에 배너로 한 번 더 표시한다.
    notConfiguredBanner = new QWidget(this);
    notConfiguredBanner->setStyleSheet(
        "background-color:#3a2e12; border:1px solid #6b5416; border-radius:8px;");
    auto *bannerLayout = new QHBoxLayout(notConfiguredBanner);
    bannerLayout->setContentsMargins(14, 10, 14, 10);
    auto *bannerLabel = new QLabel(
        "❗ 아직 평면도가 등록되지 않았습니다. \"지도 재설정\"에서 원본 이미지를 등록해주세요.",
        notConfiguredBanner);
    bannerLabel->setStyleSheet("color:#fbbf24; font-size:14px; border:none;");
    bannerLayout->addWidget(bannerLabel);
    bannerLayout->addStretch();
    auto *bannerButton = new QPushButton("지금 등록", notConfiguredBanner);
    bannerButton->setCursor(Qt::PointingHandCursor);
    bannerButton->setStyleSheet(
        "QPushButton { background-color:#fbbf24; color:#1a1400; border:none; border-radius:6px; padding:6px 14px; font-weight:bold; }"
        "QPushButton:hover { background-color:#fcd34d; }");
    connect(bannerButton, &QPushButton::clicked, this, &FloorMapPage::openSetupPanel);
    bannerLayout->addWidget(bannerButton);
    layout->addWidget(notConfiguredBanner);

    auto *card = new QFrame(this);
    card->setStyleSheet(QString("background-color:%1; border:1px solid %2; border-radius:10px;").arg(kCardBg, kCardBorder));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(4, 4, 4, 4);

    gridWidget = new FloorMapGridWidget(card);
    gridWidget->setVisible(false);
    cardLayout->addWidget(gridWidget);

    emptyStateLabel = new QLabel(
        "아직 등록된 평면도가 없습니다.\n\n"
        "우측 상단 \"지도 재설정\"에서 원본 평면도 이미지를 등록하면\n"
        "여기에 대피도가 표시됩니다.", card);
    emptyStateLabel->setAlignment(Qt::AlignCenter);
    emptyStateLabel->setStyleSheet(QString("color:%1; font-size:14px; border:none;").arg(kTextSecondary));
    cardLayout->addWidget(emptyStateLabel);

    layout->addWidget(card, 1);

    auto *hint = new QLabel("전광판(주황) 위치를 클릭하면 해당 전광판의 대피 경로가 표시됩니다.", this);
    hint->setStyleSheet(QString("color:%1; font-size:12px;").arg(kTextSecondary));
    layout->addWidget(hint);

    updateEmptyState();
}

void FloorMapPage::updateEmptyState()
{
    gridWidget->setVisible(hasData);
    emptyStateLabel->setVisible(!hasData);
    notConfiguredBanner->setVisible(!hasData);
}

void FloorMapPage::openSetupPanel()
{
    setupDialog = new QDialog(this);
    setupDialog->setWindowTitle("평면도 재설정");
    setupDialog->setStyleSheet(QString("background-color:%1;").arg(kCardBg));
    setupDialog->setMinimumWidth(640);
    auto *dialogLayout = new QVBoxLayout(setupDialog);
    dialogLayout->setContentsMargins(24, 24, 24, 24);
    dialogLayout->setSpacing(14);

    auto *desc = new QLabel(
        "원본 평면도 이미지(PNG)를 선택하면 변환 결과를 미리 확인할 수 있습니다.\n"
        "실제 변환은 서버 연동 완료 후 자동으로 이뤄지며, 지금은 확인용 예시 데이터로 표시됩니다.",
        setupDialog);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    dialogLayout->addWidget(desc);

    auto *pickBtn = new QPushButton("원본 이미지 선택...", setupDialog);
    pickBtn->setCursor(Qt::PointingHandCursor);
    pickBtn->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:white; border:none; border-radius:6px; padding:8px 16px; }"
        "QPushButton:hover { background-color:#7c6ce8; }").arg(kAccent));
    dialogLayout->addWidget(pickBtn);

    // 변환 전/후를 나란히 두고 비교 — "변환이 잘 됐는지" 확인이 이 패널의 목적.
    auto *previewRow = new QHBoxLayout;
    auto makePreviewCol = [&](const QString &label) {
        auto *col = new QVBoxLayout;
        auto *lbl = new QLabel(label, setupDialog);
        lbl->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;").arg(kTextSecondary));
        col->addWidget(lbl);
        auto *preview = new QLabel(setupDialog);
        preview->setFixedSize(280, 280);
        preview->setAlignment(Qt::AlignCenter);
        preview->setStyleSheet(QString("background-color:#0d0d16; border:1px solid %1; border-radius:6px; color:%2;").arg(kCardBorder, kTextSecondary));
        preview->setText("(이미지 없음)");
        col->addWidget(preview);
        previewRow->addLayout(col);
        return preview;
    };
    originalPreviewLabel = makePreviewCol("변환 전 (원본)");
    convertedPreviewLabel = makePreviewCol("변환 후 (대피도 미리보기)");
    dialogLayout->addLayout(previewRow);

    auto *applyBtn = new QPushButton("이 예시 데이터 적용", setupDialog);
    applyBtn->setEnabled(false);
    applyBtn->setCursor(Qt::PointingHandCursor);
    applyBtn->setStyleSheet(QString(
        "QPushButton { background-color:#232333; color:%1; border:1px solid %2; border-radius:6px; padding:8px 16px; }"
        "QPushButton:enabled:hover { border:1px solid %3; color:%3; }"
        "QPushButton:disabled { color:%4; }").arg(kTextPrimary, kCardBorder, kAccent, kTextSecondary));
    dialogLayout->addWidget(applyBtn);

    connect(pickBtn, &QPushButton::clicked, this, [this, applyBtn]() {
        const QString path = QFileDialog::getOpenFileName(this, "원본 평면도 이미지 선택", QString(), "이미지 파일 (*.png *.jpg *.jpeg)");
        if (path.isEmpty())
            return;

        const qint64 fileSize = QFileInfo(path).size();
        if (fileSize > kMaxUploadBytes) {
            QMessageBox::warning(this, "이미지 용량 초과",
                QString("선택한 이미지가 %1MB로 상한(6MB)을 초과합니다.\n"
                        "서버 전송 시 base64로 변환되면 더 커지므로 6MB 이하 이미지를 선택해주세요.")
                    .arg(fileSize / 1024.0 / 1024.0, 0, 'f', 1));
            return;
        }

        pendingOriginalImage.load(path);
        if (pendingOriginalImage.isNull()) {
            originalPreviewLabel->setText("이미지를 열 수 없습니다");
            applyBtn->setEnabled(false);
            return;
        }
        originalPreviewLabel->setPixmap(QPixmap::fromImage(pendingOriginalImage)
            .scaled(originalPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        // 실제 변환 결과가 아니라 예시 격자를 그대로 보여준다 — 서버 연동 전까지는 정직하게
        // "이게 진짜 변환 결과가 아니다"를 화면에도 남겨둔다.
        convertedPreviewLabel->setText("(서버 연동 후 실제 변환\n미리보기가 여기 표시됩니다)");
        convertedPreviewLabel->setPixmap(QPixmap());
        applyBtn->setEnabled(true);
    });

    connect(applyBtn, &QPushButton::clicked, this, [this]() {
        applyPlaceholderConversion(pendingOriginalImage);
        setupDialog->accept();
    });

    setupDialog->exec();
    setupDialog->deleteLater();
    setupDialog = nullptr;
}

void FloorMapPage::applyPlaceholderConversion(const QImage &originalImage)
{
    Q_UNUSED(originalImage); // 지금은 예시 데이터만 씀 — 실제 변환 연동 시 서버 응답으로 교체될 자리

    QVector<FloorMapMarker> displays = {
        { 1, 5, 10 },
        { 2, 50, 45 },
    };
    QVector<FloorMapMarker> exits = {
        { 1, 0, 30 },
        { 2, 61, 15 },
    };
    // 손으로 짠 L자 경로(꺾이는 지점만) — 실제 서버 연동 시 BFS 결과로 그대로 대체된다.
    QVector<FloorMapRoute> routes = {
        { 1, 1, { QPoint(10, 5), QPoint(30, 5), QPoint(30, 0) } },
        { 1, 2, { QPoint(10, 5), QPoint(10, 39), QPoint(25, 39), QPoint(15, 45), QPoint(15, 61) } },
        { 2, 1, { QPoint(45, 50), QPoint(45, 45), QPoint(54, 45), QPoint(54, 5), QPoint(30, 5), QPoint(30, 0) } },
        { 2, 2, { QPoint(45, 50), QPoint(45, 61) } },
    };

    gridWidget->setMapData(kGridSize, samplePlaceholderBitmap(), displays, exits, routes);

    const bool wasConfigured = hasData;
    hasData = true;
    updateEmptyState();
    if (!wasConfigured)
        emit configuredChanged(true);
}
