#include "VideoWidget.h"

#include <QVideoWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>

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

    video = new QVideoWidget(this);
    video->setAspectRatioMode(Qt::KeepAspectRatio);
    video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video->setStyleSheet("background-color:black;");
    layout->addWidget(video, 1);

    placeholderLabel = new QLabel("영상 연결 스트리밍", this);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet("color:#8d87a0; background-color:#0d0d16; border:none;");
    layout->addWidget(placeholderLabel, 1);
    placeholderLabel->hide();
}

void VideoWidget::setZoneName(const QString &zoneName)
{
    titleLabel->setText(QString("Ch.%1 - %2").arg(channelNumber).arg(zoneName));
}

void VideoWidget::showPlaceholder(const QString &text)
{
    lastFrame = QPixmap();
    placeholderLabel->setText(text);
    placeholderLabel->show();
    video->hide();
}

void VideoWidget::showFrame(const QImage &frame)
{
    video->hide();
    lastFrame = QPixmap::fromImage(frame);
    placeholderLabel->setPixmap(lastFrame.scaled(placeholderLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    placeholderLabel->show();
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!lastFrame.isNull())
        placeholderLabel->setPixmap(lastFrame.scaled(placeholderLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
