#include "DetectionOverlay.h"

#include <QPainter>
#include <QTimer>

DetectionOverlay::DetectionOverlay(QWidget *followTarget)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , target(followTarget)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NoSystemBackground);

    // 부모 창 이동/리사이즈까지 다 추적하려면 이벤트 필터가 필요해서, 대신 주기적으로 재동기화.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DetectionOverlay::syncGeometry);
    timer->start(200);
}

void DetectionOverlay::syncGeometry()
{
    if (!target || !target->isVisible()) {
        hide();
        return;
    }
    const QPoint topLeft = target->mapToGlobal(QPoint(0, 0));
    setGeometry(topLeft.x(), topLeft.y(), target->width(), target->height());
    if (!isVisible())
        show();
}

void DetectionOverlay::setBoxes(const QVector<DetectionBox> &newBoxes, int srcW, int srcH)
{
    boxes = newBoxes;
    srcWidth = srcW;
    srcHeight = srcH;
    update();
}

void DetectionOverlay::paintEvent(QPaintEvent *)
{
    if (boxes.isEmpty() || srcWidth <= 0 || srcHeight <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const double scaleX = double(width()) / srcWidth;
    const double scaleY = double(height()) / srcHeight;

    for (const DetectionBox &box : std::as_const(boxes)) {
        const QColor color = box.cls == "FIRE" ? QColor("#f87171") : QColor("#fb923c");
        const QRectF rect(box.x * scaleX, box.y * scaleY, box.w * scaleX, box.h * scaleY);

        painter.setPen(QPen(color, 2));
        painter.drawRect(rect);

        const QString label = QString("%1 %2%").arg(box.cls).arg(int(box.score * 100));
        painter.setPen(Qt::white);
        painter.fillRect(QRectF(rect.left(), rect.top() - 16, painter.fontMetrics().horizontalAdvance(label) + 6, 16), color);
        painter.drawText(QPointF(rect.left() + 3, rect.top() - 4), label);
    }
}
