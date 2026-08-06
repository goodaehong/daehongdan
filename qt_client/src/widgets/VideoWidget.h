#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QVector>
#include "../core/DetectionTypes.h"

class QLabel;
class QTimer;
class DetectionOverlay;

// 채널 1개 영상 + 라벨/LIVE 오버레이 + 감지 박스 오버레이. MonitorPage에서 4개 재사용.
// libvlc가 네이티브 윈도우 핸들에 직접 그리는 방식이라 QVideoWidget 대신 순수 QWidget을 렌더 타겟으로 씀.
class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(int channel, QWidget *parent = nullptr);
    ~VideoWidget() override;

    // 채널이 고정으로 감시하는 대상 이름(예: "화재감지"). 헤더에 "Ch.N · 대상"으로 표시.
    void setChannelTarget(const QString &target);
    void showPlaceholder(const QString &text);
    void showConnected();
    QWidget *videoOutput() const { return video; }
    // 이 채널에서 현재 감지 알람(계약①의 alarm 필드)이 켜져 있으면 "감지" 배지 + 테두리 강조.
    void setAlarmActive(bool active);

    // srcW/srcH: 계약①의 원본 영상 픽셀 크기. 위젯 크기에 맞게 내부에서 스케일해서 그림.
    void setDetectionBoxes(const QVector<DetectionBox> &boxes, int srcW, int srcH);

    // 사람 감지 배지. count=0 회색(없음) / count>0 초록(있음) / inDangerZone이면 빨강으로 깜빡임.
    // 실제 인식 데이터 프로토콜은 아직 미정 — UI만 먼저 만들어두고 필드 정해지면 이 API로 연결.
    void setPersonStatus(int count, bool inDangerZone);
    // 대피 음성 안내 방송 중이면 🔊 아이콘이 같이 깜빡인다. 데이터 소스 미정, UI만 우선.
    void setVoiceAnnouncementActive(bool active);

signals:
    // 카드 아무 곳이나(영상 영역 포함) 더블클릭하면 발생. MonitorPage가 받아서 확대 창을 띄운다.
    void doubleClicked(int channel);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateIndicators();

    int channelNumber;
    QString channelTarget;
    QLabel *titleLabel;
    QWidget *video;
    QLabel *placeholderLabel;
    DetectionOverlay *overlay;

    QLabel *alarmBadge;
    bool alarmActive = false;
    QLabel *personBadge;
    QLabel *speakerIcon;
    QTimer *blinkTimer;
    bool blinkOn = true;
    int personCount = 0;
    bool personInDanger = false;
    bool voiceActive = false;
};

#endif // VIDEOWIDGET_H
