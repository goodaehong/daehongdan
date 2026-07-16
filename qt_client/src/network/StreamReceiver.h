#ifndef STREAMRECEIVER_H
#define STREAMRECEIVER_H

#include <QObject>
#include <QMediaPlayer>
#include <QAudioOutput>

class QVideoWidget;

// RTSP 영상 수신 담당. 채널별 QVideoWidget에 스트림을 붙여준다.
class StreamReceiver : public QObject
{
    Q_OBJECT

public:
    explicit StreamReceiver(QObject *parent = nullptr);

    void setVideoOutput(QVideoWidget *videoWidget);
    void connectToChannel(const QString &host, const QString &user, const QString &pass, int channelIndex);

signals:
    void statusChanged(bool connected);
    void errorOccurred(const QString &message);

private:
    QMediaPlayer *player;
    QAudioOutput *audioOutput;
};

#endif // STREAMRECEIVER_H
