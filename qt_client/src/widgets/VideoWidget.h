#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>

class QLabel;
class QVideoWidget;

// 채널 1개 영상 + 라벨/LIVE 오버레이. MonitorPage에서 4개 재사용.
class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(int channel, QWidget *parent = nullptr);

    void setZoneName(const QString &zoneName);
    void showPlaceholder(const QString &text);
    QVideoWidget *videoOutput() const { return video; }

private:
    int channelNumber;
    QLabel *titleLabel;
    QVideoWidget *video;
    QLabel *placeholderLabel;
};

#endif // VIDEOWIDGET_H
