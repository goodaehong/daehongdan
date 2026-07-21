#include "StreamReceiver.h"
#include "vlc/libvlc_min.h"

#include <QWidget>

StreamReceiver::StreamReceiver(QObject *parent)
    : QObject(parent)
{
    static const char *args[] = { "--no-audio", "--quiet" };
    vlcInstance = libvlc_new(2, args);
}

StreamReceiver::~StreamReceiver()
{
    if (vlcPlayer) {
        libvlc_media_player_stop(vlcPlayer);
        libvlc_media_player_release(vlcPlayer);
    }
    if (vlcInstance)
        libvlc_release(vlcInstance);
}

void StreamReceiver::setVideoOutput(QWidget *videoWidget)
{
    targetWidget = videoWidget;
}

void StreamReceiver::connectToChannel(const QString &host, int channelIndex)
{
    if (!vlcInstance) {
        emit errorOccurred("libvlc 초기화 실패");
        return;
    }

    // MediaMTX 재배포 경로: rtsp://host:8554/camN (N은 1-based, 인증 없음)
    const QString url = QString("rtsp://%1:8554/cam%2").arg(host, QString::number(channelIndex + 1));

    libvlc_media_t *media = libvlc_media_new_location(vlcInstance, url.toUtf8().constData());
    libvlc_media_add_option(media, ":network-caching=100");
    libvlc_media_add_option(media, ":rtsp-tcp");

    vlcPlayer = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    if (targetWidget)
        libvlc_media_player_set_hwnd(vlcPlayer, reinterpret_cast<void *>(targetWidget->winId()));

    if (libvlc_media_player_play(vlcPlayer) == 0)
        emit statusChanged(true);
    else
        emit errorOccurred("재생 시작 실패");
}

