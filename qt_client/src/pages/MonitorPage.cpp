#include "MonitorPage.h"
#include "../widgets/StatusPanel.h"
#include "../widgets/VideoWidget.h"
#include "../network/StreamReceiver.h"

#include <QHBoxLayout>
#include <QGridLayout>

MonitorPage::MonitorPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(16);

    statusPanel = new StatusPanel(this);
    connect(statusPanel, &StatusPanel::demoStateRequested, this, &MonitorPage::demoStateRequested);
    layout->addWidget(statusPanel);

    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    for (int i = 0; i < 4; ++i) {
        videoWidgets[i] = new VideoWidget(i + 1, this);
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
    for (auto *v : videoWidgets)
        v->setZoneName(zone.name);
}

void MonitorPage::connectCameras(const QString &mediaMtxHost)
{
    for (int i = 0; i < 4; ++i) {
        streamReceivers[i] = new StreamReceiver(this);
        streamReceivers[i]->setVideoOutput(videoWidgets[i]->videoOutput());

        connect(streamReceivers[i], &StreamReceiver::statusChanged, this, [this, i](bool ok) {
            if (ok)
                videoWidgets[i]->showConnected();
            else
                videoWidgets[i]->showPlaceholder("연결 오류");
        });
        connect(streamReceivers[i], &StreamReceiver::errorOccurred, this, [this, i](const QString &) {
            videoWidgets[i]->showPlaceholder("연결 오류");
        });

        if (i == 0) {
            // Ch.1(A구역)만 좌측 StatusPanel의 "카메라 상태"에 반영.
            connect(streamReceivers[i], &StreamReceiver::statusChanged, this, [this](bool ok) {
                statusPanel->setCameraStatus(ok ? "정상" : "오류", ok ? "#34d399" : "#f87171");
            });
            connect(streamReceivers[i], &StreamReceiver::errorOccurred, this, [this](const QString &) {
                statusPanel->setCameraStatus("오류", "#f87171");
            });
        }

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

void MonitorPage::setActuatorStatus(int fan, int valve, int siren)
{
    statusPanel->setActuatorStatus(fan, valve, siren);
}
