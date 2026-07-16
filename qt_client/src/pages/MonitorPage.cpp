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

    for (int i = 1; i < 4; ++i)
        videoWidgets[i]->showPlaceholder("영상 연결 스트리밍");
}

void MonitorPage::updateZone(const Zone &zone)
{
    statusPanel->updateZone(zone);
    for (auto *v : videoWidgets)
        v->setZoneName(zone.name);
}

void MonitorPage::connectCamera(const QString &host, const QString &user, const QString &pass, int channelIndex)
{
    streamReceiver = new StreamReceiver(this);
    streamReceiver->setVideoOutput(videoWidgets[0]->videoOutput());
    connect(streamReceiver, &StreamReceiver::statusChanged, this, [this](bool ok) {
        statusPanel->setCameraStatus(ok ? "정상" : "오류", ok ? "#34d399" : "#f87171");
    });
    connect(streamReceiver, &StreamReceiver::errorOccurred, this, [this](const QString &) {
        statusPanel->setCameraStatus("오류", "#f87171");
    });
    streamReceiver->connectToChannel(host, user, pass, channelIndex);
}
