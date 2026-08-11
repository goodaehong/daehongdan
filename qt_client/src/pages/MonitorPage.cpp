#include "MonitorPage.h"
#include "../widgets/StatusPanel.h"
#include "../widgets/VideoWidget.h"
#include "../network/StreamReceiver.h"

#include <QHBoxLayout>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QDialog>

MonitorPage::MonitorPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    statusPanel = new StatusPanel(this);
    connect(statusPanel, &StatusPanel::demoStateRequested, this, &MonitorPage::demoStateRequested);
    connect(statusPanel, &StatusPanel::controlActionRequested, this, &MonitorPage::controlActionRequested);
    connect(statusPanel, &StatusPanel::evacuationActionRequested, this, &MonitorPage::evacuationActionRequested);
    layout->addWidget(statusPanel);

    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    for (int i = 0; i < 4; ++i) {
        videoWidgets[i] = new VideoWidget(i + 1, this);
        videoWidgets[i]->setChannelTarget(channelTargetName(i + 1));
        connect(videoWidgets[i], &VideoWidget::doubleClicked, this, &MonitorPage::showEnlargedView);
        grid->addWidget(videoWidgets[i], i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);
    layout->addLayout(grid, 1);
}

void MonitorPage::updateZone(const Zone &zone)
{
    statusPanel->updateZone(zone);
}

void MonitorPage::connectCameras(const QString &mediaMtxHost)
{
    this->mediaMtxHost = mediaMtxHost;
    for (int i = 0; i < 4; ++i) {
        streamReceivers[i] = new StreamReceiver(this);
        streamReceivers[i]->setVideoOutput(videoWidgets[i]->videoOutput());

        connect(streamReceivers[i], &StreamReceiver::statusChanged, this, [this, i](bool ok) {
            if (ok)
                videoWidgets[i]->showConnected();
            else
                videoWidgets[i]->showPlaceholder("신호 없음");
            statusPanel->setCameraChannelStatus(i + 1, ok);
        });
        connect(streamReceivers[i], &StreamReceiver::errorOccurred, this, [this, i](const QString &) {
            videoWidgets[i]->showPlaceholder("신호 없음");
            statusPanel->setCameraChannelStatus(i + 1, false);
        });

        streamReceivers[i]->connectToChannel(mediaMtxHost, i);
    }
}

void MonitorPage::updateDetection(int channel, int srcW, int srcH, const QVector<DetectionBox> &boxes)
{
    const int index = channel - 1;
    if (index < 0 || index >= 4)
        return;
    videoWidgets[index]->setDetectionBoxes(boxes, srcW, srcH);
}

void MonitorPage::setChannelAlarm(int channel, bool active)
{
    const int index = channel - 1;
    if (index < 0 || index >= 4)
        return;
    videoWidgets[index]->setAlarmActive(active);
}

void MonitorPage::setActuatorStatus(int fan, int valve, int siren, const QString &link,
                                     const QString &fanSrc, const QString &valveSrc, const QString &sirenSrc)
{
    statusPanel->setActuatorStatus(fan, valve, siren, link, fanSrc, valveSrc, sirenSrc);
}

void MonitorPage::showControlStatus(const QString &text, const QString &color)
{
    statusPanel->showCommandStatus(text, color);
}

void MonitorPage::setActuatorRowStatus(const QString &target, const QString &text, const QString &color)
{
    statusPanel->setActuatorRowStatus(target, text, color);
}

void MonitorPage::showEnlargedView(int channel)
{
    if (mediaMtxHost.isEmpty())
        return; // 아직 카메라 연결 전

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QString("Ch.%1 확대 보기").arg(channel));
    dialog->resize(960, 720);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setStyleSheet("background-color:#0a0a12;");

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(12, 12, 12, 12);

    // 같은 채널을 새로 하나 더 연결(MediaMTX는 다중 접속을 지원). 기존 작은 화면과는 독립적.
    auto *bigVideo = new VideoWidget(channel, dialog);
    bigVideo->setChannelTarget(channelTargetName(channel));
    layout->addWidget(bigVideo);

    auto *receiver = new StreamReceiver(dialog);
    receiver->setVideoOutput(bigVideo->videoOutput());
    connect(receiver, &StreamReceiver::statusChanged, bigVideo, [bigVideo](bool ok) {
        if (ok)
            bigVideo->showConnected();
        else
            bigVideo->showPlaceholder("신호 없음");
    });
    connect(receiver, &StreamReceiver::errorOccurred, bigVideo, [bigVideo](const QString &) {
        bigVideo->showPlaceholder("신호 없음");
    });
    receiver->connectToChannel(mediaMtxHost, channel - 1);

    dialog->show();
}
