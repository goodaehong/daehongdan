#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QWidget>
#include "../core/ZoneTypes.h"

class StatusPanel;
class VideoWidget;
class DetectionStreamClient;

// 모니터링 화면: 좌측 StatusPanel + 우측 2x2 카메라 그리드(VideoWidget x4).
// Ch.1은 fire_bridge_server(OpenCV 화재감지)가 보내는 오버레이 영상을 표시한다.
class MonitorPage : public QWidget
{
    Q_OBJECT

public:
    explicit MonitorPage(QWidget *parent = nullptr);

    void updateZone(const Zone &zone);
    void connectDetectionStream(const QString &host, quint16 port);

signals:
    void demoStateRequested(ZoneState state);

private:
    StatusPanel *statusPanel;
    VideoWidget *videoWidgets[4];
    DetectionStreamClient *detectionStream;
};

#endif // MONITORPAGE_H
