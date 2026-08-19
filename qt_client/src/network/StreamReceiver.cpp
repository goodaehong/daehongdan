#include "StreamReceiver.h"
#include "vlc/libvlc_min.h"

#include <QWidget>
#include <QResizeEvent>

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
    if (targetWidget)
        targetWidget->removeEventFilter(this);
    targetWidget = videoWidget;
    if (targetWidget)
        targetWidget->installEventFilter(this); // 줌/창 크기 변화 시 크롭 비율 재적용
}

bool StreamReceiver::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == targetWidget && event->type() == QEvent::Resize)
        applyCropGeometry();
    return QObject::eventFilter(watched, event);
}

void StreamReceiver::applyCropGeometry()
{
    if (!vlcPlayer || !targetWidget)
        return;
    const QSize size = targetWidget->size();
    if (size.width() <= 0 || size.height() <= 0)
        return;
    // 렌더 타겟과 정확히 같은 비율로 잘라내면 libvlc가 원본 비율 유지(레터박스) 없이 타겟을
    // 꽉 채운다 — 카메라 원본 비율이 타일 비율과 달라도 화면에 검은 여백이 안 남는다.
    const QString geometry = QString("%1:%2").arg(size.width()).arg(size.height());
    libvlc_video_set_crop_geometry(vlcPlayer, geometry.toUtf8().constData());
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
    libvlc_media_add_option(media, ":network-caching=500");
    libvlc_media_add_option(media, ":rtsp-tcp");

    vlcPlayer = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    if (targetWidget)
        libvlc_media_player_set_hwnd(vlcPlayer, reinterpret_cast<void *>(targetWidget->winId()));
    applyCropGeometry(); // 이후엔 targetWidget 리사이즈 이벤트가 알아서 갱신해줌

    if (libvlc_media_player_play(vlcPlayer) == 0)
        emit statusChanged(true);
    else
        emit errorOccurred("재생 시작 실패");
}

