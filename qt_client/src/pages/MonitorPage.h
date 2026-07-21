#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QWidget>
#include <QVector>
#include "../core/ZoneTypes.h"
#include "../core/DetectionTypes.h"

class StatusPanel;
class VideoWidget;
class StreamReceiver;

// 모니터링 화면: 좌측 StatusPanel + 우측 2x2 카메라 그리드(VideoWidget x4).
class MonitorPage : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorPage(QWidget *parent = nullptr);

    void updateZone(const Zone &zone);
    // MediaMTX 재배포 서버 주소 하나로 4채널(cam1~cam4) 전부 연결.
    void connectCameras(const QString &mediaMtxHost);

    // 계약① 감지결과 수신 시 MainWindow가 호출. channel은 1-based(1~4).
    void updateDetection(int channel, int srcW, int srcH, const QVector<DetectionBox> &boxes);

signals:
    void demoStateRequested(ZoneState state);

private:
    StatusPanel *statusPanel;
    VideoWidget *videoWidgets[4];
    StreamReceiver *streamReceivers[4];
};

#endif // MONITORPAGE_H
