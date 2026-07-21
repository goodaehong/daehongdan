#include "VideoWidget.h"
#include "DetectionOverlay.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

VideoWidget::VideoWidget(int channel, QWidget *parent)
    : QWidget(parent)
    , channelNumber(channel)
{
    setStyleSheet("background-color:#14141f; border:1px solid #232333; border-radius:8px;");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);

    auto *header = new QHBoxLayout;
    titleLabel = new QLabel(QString("Ch.%1").arg(channel), this);
    titleLabel->setStyleSheet("color:#8d87a0; font-size:12px; border:none;");
    header->addWidget(titleLabel);
    header->addStretch();
    auto *liveBadge = new QLabel("● LIVE", this);
    liveBadge->setStyleSheet("color:#f87171; font-size:11px; font-weight:bold; border:none;");
    header->addWidget(liveBadge);
    layout->addLayout(header);

    // libvlc가 이 위젯의 네이티브 HWND에 직접 그림 (Qt가 그 위에 페인트하면 안 됨).
    video = new QWidget(this);
    video->setAttribute(Qt::WA_NativeWindow);
    video->setAttribute(Qt::WA_PaintOnScreen);
    video->setAttribute(Qt::WA_NoSystemBackground);
    video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video->setStyleSheet("background-color:black;");
    layout->addWidget(video, 1);

    placeholderLabel = new QLabel("연결 중...", this);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet("color:#8d87a0; background-color:#0d0d16; border:none;");
    layout->addWidget(placeholderLabel, 1);

    video->hide();

    // 감지 박스 오버레이: video와 같은 부모에 두지 않고 독립된 최상위 창으로 추적시킴.
    overlay = new DetectionOverlay(video);
}

VideoWidget::~VideoWidget()
{
    delete overlay; // 최상위 창(부모 없음)이라 Qt가 자동으로 안 지워줌
}

void VideoWidget::setZoneName(const QString &zoneName)
{
    titleLabel->setText(QString("Ch.%1 - %2").arg(channelNumber).arg(zoneName));
}

void VideoWidget::showPlaceholder(const QString &text)
{
    placeholderLabel->setText(text);
    placeholderLabel->show();
    video->hide();
    overlay->hide();
}

void VideoWidget::showConnected()
{
    placeholderLabel->hide();
    video->show();
    overlay->syncGeometry();
}

void VideoWidget::setDetectionBoxes(const QVector<DetectionBox> &boxes, int srcW, int srcH)
{
    overlay->setBoxes(boxes, srcW, srcH);
}
