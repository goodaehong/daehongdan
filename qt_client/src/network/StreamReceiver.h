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

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 타겟 위젯(VideoWidget::video, 줌/이동에 따라 크기가 바뀜) 비율로 크롭 지오메트리를 다시
    // 세팅한다 — 렌더 타겟과 크롭 비율을 항상 똑같이 맞춰야 libvlc가 레터박스(검은 여백) 없이
    // 꽉 채운다. 위젯이 리사이즈될 때마다(줌 변경, 창 크기 조절, 확대 보기 전환 등) 다시 불린다.
    void applyCropGeometry();

    libvlc_instance_t *vlcInstance = nullptr;
    libvlc_media_player_t *vlcPlayer = nullptr;
    QWidget *targetWidget = nullptr;
};

#endif // STREAMRECEIVER_H
