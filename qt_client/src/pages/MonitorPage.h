#ifndef MONITORPAGE_H
#define MONITORPAGE_H

#include <QWidget>
#include "../core/ZoneTypes.h"

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
    void connectCamera(const QString &host, const QString &user, const QString &pass, int channelIndex);

signals:
    void demoStateRequested(ZoneState state);

private:
    StatusPanel *statusPanel;
    VideoWidget *videoWidgets[4];
    StreamReceiver *streamReceiver;
};

#endif // MONITORPAGE_H
