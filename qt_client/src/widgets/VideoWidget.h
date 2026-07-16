#ifndef VIDEOWIDGET_H
#define VIDEOWIDGET_H

#include <QWidget>
#include <QImage>
#include <QPixmap>

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
    void showFrame(const QImage &frame);
    QVideoWidget *videoOutput() const { return video; }

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    int channelNumber;
    QLabel *titleLabel;
    QVideoWidget *video;
    QLabel *placeholderLabel;
    QPixmap lastFrame;
};

#endif // VIDEOWIDGET_H
