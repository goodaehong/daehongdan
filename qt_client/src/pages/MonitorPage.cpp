#include "MonitorPage.h"
#include "../widgets/StatusPanel.h"
#include "../widgets/VideoWidget.h"
#include "../network/DetectionStreamClient.h"

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

    videoWidgets[0]->showPlaceholder("감지 서버 연결 대기중...");
    for (int i = 1; i < 4; ++i)
        videoWidgets[i]->showPlaceholder("영상 연결 스트리밍");
}

void MonitorPage::updateZone(const Zone &zone)
{
    statusPanel->updateZone(zone);
    for (auto *v : videoWidgets)
        v->setZoneName(zone.name);
}

void MonitorPage::connectDetectionStream(const QString &host, quint16 port)
{
    detectionStream = new DetectionStreamClient(this);

    connect(detectionStream, &DetectionStreamClient::frameReady, this,
            [this](const QImage &frame, bool alarmActive) {
                videoWidgets[0]->showFrame(frame);
                statusPanel->setCameraStatus(alarmActive ? "경고" : "정상", alarmActive ? "#f87171" : "#34d399");
            });
    connect(detectionStream, &DetectionStreamClient::connectionStateChanged, this,
            [this](bool connected) {
                if (!connected)
                    videoWidgets[0]->showPlaceholder("감지 서버 연결 끊김");
            });

    detectionStream->connectToServer(host, port);
}
