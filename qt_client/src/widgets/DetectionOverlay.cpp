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
    // 그릴 박스가 없을 땐 아예 화면에 안 띄운다 — 계속 떠있는 빈 always-on-top 창이
    // Windows 레이어드 창 컴포지팅 이슈로 다른 앱 위에 테두리 잔상을 남기는 걸 방지.
    if (!target || !target->isVisible() || boxes.isEmpty()) {
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
    syncGeometry(); // 박스가 비었으면 즉시 숨기고, 생겼으면 바로 보여준다 (다음 200ms 폴링까지 안 기다림)
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
        QColor color;
        if (box.cls == "FIRE") color = QColor("#f87171");
        else if (box.cls == "PERSON") color = QColor("#38bdf8"); // 화재/연기(빨강/주황)와 구분되는 하늘색
        else color = QColor("#fb923c"); // SMOKE 등
        const QRectF rect(box.x * scaleX, box.y * scaleY, box.w * scaleX, box.h * scaleY);

        painter.setPen(QPen(color, 2));
        painter.drawRect(rect);

        const QString label = QString("%1 %2%").arg(box.cls).arg(int(box.score * 100));
        painter.setPen(Qt::white);
        painter.fillRect(QRectF(rect.left(), rect.top() - 16, painter.fontMetrics().horizontalAdvance(label) + 6, 16), color);
        painter.drawText(QPointF(rect.left() + 3, rect.top() - 4), label);
    }
}
