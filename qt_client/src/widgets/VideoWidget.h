#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPoint>
#include <QRectF>
#include <QPolygonF>
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

    // 사람 감지(명세서 3번 계약). 박스는 화재/연기 박스와 같은 오버레이에 겹쳐 그리고(cls="PERSON",
    // DetectionOverlay가 하늘색으로 구분해서 그림), count/위험여부는 상단 배지(setPersonStatus)에
    // 반영한다. "위험 중 사람 있음"의 위험 여부는 이 위젯이 이미 들고 있는 alarmActive를 그대로
    // 쓴다 — 호출자(MonitorPage)가 따로 채널별 상태를 안 들고 있어도 됨.
    void setPersonBoxes(const QVector<DetectionBox> &boxes, int srcW, int srcH, int count);

    // 사람 감지 배지. count=0 회색(없음) / count>0 초록(있음) / inDangerZone이면 빨강으로 깜빡임.
    void setPersonStatus(int count, bool inDangerZone);
    // 대피 음성 안내 방송 중이면 🔊 아이콘이 같이 깜빡인다. 데이터 소스 미정, UI만 우선.
    void setVoiceAnnouncementActive(bool active);

    // 서버에서 받은(접속 직후 push 또는 query 응답) ROI로 화면을 갱신한다. 편집 중이면 사용자가
    // 그리고 있는 걸 덮어쓰면 안 되므로 무시한다.
    void setRoiRegionsFromServer(const QVector<RoiRegion> &regions);

signals:
    // 카드 아무 곳이나(영상 영역 포함) 더블클릭하면 발생. MonitorPage가 받아서 확대 창을 띄운다.
    void doubleClicked(int channel);
    // ROI 편집을 마칠 때(편집 버튼을 다시 눌러 끌 때) 발생. MonitorPage가 받아서 서버로 전송한다.
    void roiRegionsChanged(int channel, const QVector<RoiRegion> &regions);

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
    // detectionBoxesCache/personBoxesCache를 합쳐 overlay->setBoxes() 한 번으로 넘긴다 —
    // 두 종류가 독립된 메시지로 각자 다른 주기에 오므로, 최신값을 각자 들고 있다가 합쳐야
    // 한쪽이 갱신될 때 다른 쪽이 화면에서 사라지지 않는다.
    void refreshOverlayBoxes(int srcW, int srcH);
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
    QVector<DetectionBox> detectionBoxesCache;
    QVector<DetectionBox> personBoxesCache;

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
    QPushButton *roiVisibilityButton;
    bool roiEditActive = false;
    QVector<RoiRegion> savedRoiRegions; // 0~1 정규화, zoom=1.0/pan=0 기준. 각 영역 = 꼭짓점 4개 + 적용 대상
};

#endif // VIDEOWIDGET_H
