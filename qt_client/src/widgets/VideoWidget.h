#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QVector>
#include "../core/DetectionTypes.h"

class QLabel;
class DetectionOverlay;

// 채널 1개 영상 + 라벨/LIVE 오버레이 + 감지 박스 오버레이. MonitorPage에서 4개 재사용.
// libvlc가 네이티브 윈도우 핸들에 직접 그리는 방식이라 QVideoWidget 대신 순수 QWidget을 렌더 타겟으로 씀.
class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(int channel, QWidget *parent = nullptr);
    ~VideoWidget() override;

    void setZoneName(const QString &zoneName);
    void showPlaceholder(const QString &text);
    void showConnected();
    QWidget *videoOutput() const { return video; }

    // srcW/srcH: 계약①의 원본 영상 픽셀 크기. 위젯 크기에 맞게 내부에서 스케일해서 그림.
    void setDetectionBoxes(const QVector<DetectionBox> &boxes, int srcW, int srcH);

private:
    int channelNumber;
    QLabel *titleLabel;
    QWidget *video;
    QLabel *placeholderLabel;
    DetectionOverlay *overlay;
};

#endif // VIDEOWIDGET_H
