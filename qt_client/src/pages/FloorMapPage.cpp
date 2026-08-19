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
#include <QStringList>
#include <QBuffer>

namespace {
const QString kTextPrimary = "#f5f5fa";
const QString kTextSecondary = "#8d87a0";
const QString kCardBg = "#14141f";
const QString kCardBorder = "#232333";
const QString kAccent = "#8b7cf6";

// 서버가 base64 전송 줄 길이를 8MB로 제한(PR #56). base64는 원본의 약 1.33배이므로
// 원본 기준 6MB를 상한으로 둔다.
constexpr qint64 kMaxUploadBytes = 6LL * 1024 * 1024;
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

void FloorMapPage::setDialogBusy(bool busy, const QString &statusText)
{
    if (!setupDialog)
        return; // 응답이 늦게 와서 다이얼로그가 이미 닫힌 경우 — 조용히 무시
    if (pickButton) pickButton->setEnabled(!busy);
    if (applyButton) applyButton->setEnabled(!busy && !pendingOriginalImage.isNull());
    if (convertedPreviewLabel) convertedPreviewLabel->setText(statusText);
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
        "원본 평면도 이미지(PNG/JPG)를 선택하고 적용하면 서버로 전송해 변환합니다.\n"
        "변환에는 몇 초 정도 걸릴 수 있습니다 — 완료되면 이 창이 자동으로 닫히고 화면에 반영됩니다.",
        setupDialog);
    desc->setWordWrap(true);
    desc->setStyleSheet(QString("color:%1; font-size:13px;").arg(kTextSecondary));
    dialogLayout->addWidget(desc);

    pickButton = new QPushButton("원본 이미지 선택...", setupDialog);
    pickButton->setCursor(Qt::PointingHandCursor);
    pickButton->setStyleSheet(QString(
        "QPushButton { background-color:%1; color:white; border:none; border-radius:6px; padding:8px 16px; }"
        "QPushButton:hover { background-color:#7c6ce8; }"
        "QPushButton:disabled { background-color:#3a3550; color:%2; }").arg(kAccent, kTextSecondary));
    dialogLayout->addWidget(pickButton);

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
    originalPreviewLabel = makePreviewCol("원본");
    convertedPreviewLabel = makePreviewCol("서버 처리 상태");
    dialogLayout->addLayout(previewRow);

    applyButton = new QPushButton("서버로 전송 및 적용", setupDialog);
    applyButton->setEnabled(false);
    applyButton->setCursor(Qt::PointingHandCursor);
    applyButton->setStyleSheet(QString(
        "QPushButton { background-color:#232333; color:%1; border:1px solid %2; border-radius:6px; padding:8px 16px; }"
        "QPushButton:enabled:hover { border:1px solid %3; color:%3; }"
        "QPushButton:disabled { color:%4; }").arg(kTextPrimary, kCardBorder, kAccent, kTextSecondary));
    dialogLayout->addWidget(applyButton);

    connect(pickButton, &QPushButton::clicked, this, [this]() {
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
            applyButton->setEnabled(false);
            return;
        }
        originalPreviewLabel->setPixmap(QPixmap::fromImage(pendingOriginalImage)
            .scaled(originalPreviewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        convertedPreviewLabel->setText("(적용을 누르면 서버로 전송합니다)");
        convertedPreviewLabel->setPixmap(QPixmap());
        applyButton->setEnabled(true);
    });

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        // 원본이 JPG여도 서버는 PNG 시그니처를 검사하므로(FloorMapStore_Apply) 항상 PNG로 재인코딩한다.
        QByteArray pngBytes;
        QBuffer buffer(&pngBytes);
        buffer.open(QIODevice::WriteOnly);
        pendingOriginalImage.save(&buffer, "PNG");
        buffer.close();

        setDialogBusy(true, "서버로 전송 중...\n(이미지 저장 + 대피경로 변환)");
        emit floorMapUploadRequested(pngBytes);
    });

    setupDialog->exec();
    setupDialog->deleteLater();
    setupDialog = nullptr;
    originalPreviewLabel = nullptr;
    convertedPreviewLabel = nullptr;
    pickButton = nullptr;
    applyButton = nullptr;
    pendingOriginalImage = QImage();
}

void FloorMapPage::applyServerData(int gridSize, const QVector<QVector<int>> &bitmap,
                                    const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                                    const QVector<FloorMapRoute> &routes)
{
    gridWidget->setMapData(gridSize, bitmap, displays, exits, routes);

    const bool wasConfigured = hasData;
    hasData = true;
    updateEmptyState();
    if (!wasConfigured)
        emit configuredChanged(true);
}

void FloorMapPage::onUploadResult(bool ok, const QString &reason, int gridSize, const QVector<QVector<int>> &bitmap,
                                   const QVector<FloorMapMarker> &displays, const QVector<FloorMapMarker> &exits,
                                   const QVector<FloorMapRoute> &routes)
{
    if (ok) {
        applyServerData(gridSize, bitmap, displays, exits, routes);
        if (setupDialog)
            setupDialog->accept(); // exec() 리턴 -> openSetupPanel()의 뒷정리로 이어짐
        return;
    }

    if (!setupDialog)
        return; // 다이얼로그를 이미 닫은 뒤 응답이 온 경우 — 화면엔 반영할 게 없으니 조용히 무시
    setDialogBusy(false, QString("변환 실패: %1\n\n다른 이미지로 다시 시도해주세요.")
                              .arg(reason.isEmpty() ? "알 수 없는 오류" : reason));
}

void FloorMapPage::onUploadTimedOut()
{
    if (!setupDialog)
        return;
    setDialogBusy(false, "응답 없음 — 서버 연결을 확인해주세요.");
}

