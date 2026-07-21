#ifndef STREAMRECEIVER_H
#define STREAMRECEIVER_H

#include <QObject>

class QWidget;
struct libvlc_instance_t;
struct libvlc_media_player_t;

// RTSP 영상 수신 담당 (libvlc 기반). 채널별 네이티브 위젯에 스트림을 붙여준다.
class StreamReceiver : public QObject
{
    Q_OBJECT

public:
    explicit StreamReceiver(QObject *parent = nullptr);
    ~StreamReceiver() override;

    void setVideoOutput(QWidget *videoWidget);
    // channelIndex: 0-based (0~3). MediaMTX 경로는 1-based(cam1~cam4)라 내부에서 +1 변환.
    void connectToChannel(const QString &host, int channelIndex);

signals:
    void statusChanged(bool connected);
    void errorOccurred(const QString &message);

private:
    libvlc_instance_t *vlcInstance = nullptr;
    libvlc_media_player_t *vlcPlayer = nullptr;
    QWidget *targetWidget = nullptr;
};

#endif // STREAMRECEIVER_H
