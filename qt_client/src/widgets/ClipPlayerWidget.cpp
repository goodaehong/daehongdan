#include "ClipPlayerWidget.h"
#include "../network/vlc/libvlc_min.h"

#include <QUrl>
#include <QTimer>

ClipPlayerWidget::ClipPlayerWidget(QWidget *parent)
    : QWidget(parent)
{
    // StreamReceiver의 video 렌더 타겟과 동일한 속성 — libvlc가 네이티브 윈도우 핸들에 직접 그린다.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setStyleSheet("background-color:black;");

    static const char *args[] = { "--quiet" };
    vlcInstance = libvlc_new(1, args);

    pollTimer = new QTimer(this);
    pollTimer->setInterval(200);
    connect(pollTimer, &QTimer::timeout, this, &ClipPlayerWidget::poll);
}

ClipPlayerWidget::~ClipPlayerWidget()
{
    stop();
    if (vlcInstance)
        libvlc_release(vlcInstance);
}

void ClipPlayerWidget::playFile(const QString &filePath)
{
    if (!vlcInstance)
        return;
    stop();

    // libvlc_media_new_path 심볼은 CMakeLists.txt의 def 화이트리스트에 없음(RTSP만 쓰던 시절
    // libvlc_media_new_location만 뚫어놨음) — "file://" MRL도 libvlc_media_new_location이 그대로
    // 처리하므로 새 심볼을 안 뚫고 재사용한다.
    const QString mrl = QUrl::fromLocalFile(filePath).toString();
    libvlc_media_t *media = libvlc_media_new_location(vlcInstance, mrl.toUtf8().constData());
    vlcPlayer = libvlc_media_player_new_from_media(media);
    libvlc_media_release(media);

    libvlc_media_player_set_hwnd(vlcPlayer, reinterpret_cast<void *>(winId()));
    libvlc_media_player_play(vlcPlayer);
    pollTimer->start();
}

void ClipPlayerWidget::stop()
{
    pollTimer->stop();
    if (!vlcPlayer)
        return;
    libvlc_media_player_stop(vlcPlayer);
    libvlc_media_player_release(vlcPlayer);
    vlcPlayer = nullptr;
}

void ClipPlayerWidget::seek(qint64 ms)
{
    if (vlcPlayer)
        libvlc_media_player_set_time(vlcPlayer, ms);
}

void ClipPlayerWidget::poll()
{
    if (!vlcPlayer)
        return;
    emit positionChanged(libvlc_media_player_get_time(vlcPlayer), libvlc_media_player_get_length(vlcPlayer));
}
