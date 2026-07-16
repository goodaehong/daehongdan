#include "StreamReceiver.h"

#include <QVideoWidget>
#include <QUrl>

StreamReceiver::StreamReceiver(QObject *parent)
    : QObject(parent)
{
    player = new QMediaPlayer(this);
    audioOutput = new QAudioOutput(this);
    audioOutput->setMuted(true);
    player->setAudioOutput(audioOutput);

    connect(player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) {
                emit statusChanged(false);
                emit errorOccurred(message);
            });
    connect(player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus status) {
                if (status == QMediaPlayer::BufferedMedia || status == QMediaPlayer::LoadedMedia)
                    emit statusChanged(true);
            });
}

void StreamReceiver::setVideoOutput(QVideoWidget *videoWidget)
{
    player->setVideoOutput(videoWidget);
}

void StreamReceiver::connectToChannel(const QString &host, const QString &user, const QString &pass, int channelIndex)
{
    const QString url = QString("rtsp://%1:%2@%3:554/%4/profile3/media.smp")
                             .arg(user, pass, host, QString::number(channelIndex));
    player->setSource(QUrl(url));
    player->play();
}
