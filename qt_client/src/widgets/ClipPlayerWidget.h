#ifndef CLIPPLAYERWIDGET_H
#define CLIPPLAYERWIDGET_H

#include <QWidget>

class QTimer;
struct libvlc_instance_t;
struct libvlc_media_player_t;

// 이벤트 클립(mp4) 로컬 파일 재생 전용 위젯. StreamReceiver(RTSP 실시간 스트림)와 같은 libvlc
// 네이티브 윈도우 임베딩 방식을 쓰되, 서버에서 그때그때 받아 임시 저장한 mp4 파일 하나를 재생한다 —
// 이벤트로그 "재생" 클릭 시 OS 기본 플레이어(새 창)로 열던 것을 Qt 안에서 바로 보이게 하기 위함.
class ClipPlayerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ClipPlayerWidget(QWidget *parent = nullptr);
    ~ClipPlayerWidget() override;

    void playFile(const QString &filePath);
    void stop();
    // 재생바 드래그로 임의 지점부터 재생 — libvlc는 재생 중이 아니어도(멈춰있어도) set_time 자체는
    // 받아주지만, 이 위젯은 항상 playFile()로 시작해서 재생 중인 상태로만 쓰인다.
    void seek(qint64 ms);

signals:
    // 재생 중 200ms 간격으로 현재 위치/전체 길이를 알려준다(libvlc 3.x 이벤트 콜백 대신 폴링 —
    // 콜백은 별도 스레드에서 오므로 Qt 위젯을 직접 건드리려면 큐드 커넥션 등 더 다뤄야 할 게 많아서
    // 클립이 13초 안팎으로 짧은 이 용도엔 폴링이 훨씬 간단하고 충분히 부드럽다).
    // lengthMs가 아직 파악 전이면 0으로 온다(재생 시작 직후 한두 tick).
    void positionChanged(qint64 currentMs, qint64 lengthMs);

private:
    void poll();

    libvlc_instance_t *vlcInstance = nullptr;
    libvlc_media_player_t *vlcPlayer = nullptr;
    QTimer *pollTimer = nullptr;
};

#endif // CLIPPLAYERWIDGET_H
