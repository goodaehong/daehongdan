#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QRectF>
#include "../core/DetectionTypes.h"

class QLabel;
class QTimer;
class QPushButton;
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
    void changeZoom(int direction);
    void applyPanDelta(const QPoint &delta);
    // ROI(감시 제외 영역) 편집 모드 토글. 켜면 줌/이동을 1.0/0으로 리셋하고 줌 버튼을 잠근다 —
    // 편집 중 좌표 기준(zoom=1.0/pan=0)이 바뀌면 이미 그린 영역이 화면과 안 맞게 되기 때문.
    // 오늘 범위: 서버 전송 없이 세션 동안만(재실행하면 초기화) 채널별로 로컬에 들고 있는다.
    void toggleRoiEdit();
    // zoomFactor/panX/panY 상태를 실제 video 위젯의 크기/위치로 반영한다. 확대 배율이 바뀌거나
    // videoViewport가 리사이즈될 때(창 크기 조절, 확대 다이얼로그 전환 등) 호출된다.
    // libvlc 크롭 API는 전혀 쓰지 않는다 — 원본 프레임은 그대로 디코딩시키고, video 네이티브
    // 창 자체를 확대/이동시켜서 videoViewport 밖으로 나간 부분은 OS가 자동으로 잘라준다.
    // 이렇게 하면 드래그할 때마다 영상 디코드 파이프라인을 건드릴 일이 없어 깜빡임이 없다.
    void updateVideoTransform();

    int channelNumber;
    QString channelTarget;
    QLabel *titleLabel;
    // 화면에 보이는 고정 크기 영역(클리핑 뷰포트). 실제 video는 이 안에서 확대/이동한다.
    QWidget *videoViewport;
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
    QLabel *zoomLabel;
    QPushButton *zoomOutBtn;
    QPushButton *zoomInBtn;
    double zoomFactor = 1.0;
    double panX = 0.0;
    double panY = 0.0;

    QPushButton *roiButton;
    bool roiEditActive = false;
    QVector<QRectF> savedRoiRegions; // 0~1 정규화, zoom=1.0/pan=0 기준
};

#endif // VIDEOWIDGET_H
