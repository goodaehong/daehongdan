#include "DetectionOverlay.h"

#include <QPainter>
#include <QTimer>
#include <QMouseEvent>
#include <QtGlobal>

DetectionOverlay::DetectionOverlay(QWidget *followTarget)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , target(followTarget)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NoSystemBackground);

    // VLC 네이티브 자식 창보다 위에서 마우스를 받는 입력 전용 창.
    // 감지 박스 창과 분리하여 투명 입력 속성을 실행 중에 바꾸지 않아도 되게 한다.
    inputSurface = new QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    inputSurface->setAttribute(Qt::WA_ShowWithoutActivating);
    inputSurface->setStyleSheet("background-color:#000000;");
    inputSurface->setWindowOpacity(0.01);
    inputSurface->setCursor(Qt::OpenHandCursor);
    inputSurface->installEventFilter(this);
    inputSurface->hide();

    // 부모 창 이동/리사이즈까지 다 추적하려면 이벤트 필터가 필요해서, 대신 주기적으로 재동기화.
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &DetectionOverlay::syncGeometry);
    timer->start(200);
}

DetectionOverlay::~DetectionOverlay()
{
    delete inputSurface;
}

void DetectionOverlay::syncGeometry()
{
    // 그릴 박스가 없을 땐 아예 화면에 안 띄운다 — 계속 떠있는 빈 always-on-top 창이
    // Windows 레이어드 창 컴포지팅 이슈로 다른 앱 위에 테두리 잔상을 남기는 걸 방지.
    if (!target || !target->isVisible()) {
        hide();
        inputSurface->hide();
        return;
    }
    const QPoint topLeft = target->mapToGlobal(QPoint(0, 0));
    const QRect targetGeometry(topLeft, target->size());
    if (boxes.isEmpty()) {
        hide();
    } else {
        setGeometry(targetGeometry);
        if (!isVisible())
            show();
    }
    // 드래그 중엔 target(영상)이 움직이지 않으므로 재배치가 필요 없다. 오히려 grabMouse() 중인
    // 창을 setGeometry()/raise()로 계속 건드리면 Windows에서 마우스 캡처가 순간적으로 흔들려
    // 드래그가 뚝뚝 끊기거나 위치가 튀는 현상이 생긴다 — 그래서 드래그 중엔 건드리지 않는다.
    if (dragging) {
        // no-op
    } else if (interactionEnabled) {
        inputSurface->setGeometry(targetGeometry);
        if (!inputSurface->isVisible())
            inputSurface->show();
        inputSurface->raise();
    } else {
        inputSurface->hide();
    }
}

void DetectionOverlay::setBoxes(const QVector<DetectionBox> &newBoxes, int srcW, int srcH)
{
    boxes = newBoxes;
    srcWidth = srcW;
    srcHeight = srcH;
    update();
    syncGeometry(); // 박스가 비었으면 즉시 숨기고, 생겼으면 바로 보여준다 (다음 200ms 폴링까지 안 기다림)
}

void DetectionOverlay::setViewTransform(double factor, double x, double y)
{
    zoomFactor = qBound(1.0, factor, 2.5);
    panX = qBound(-1.0, x, 1.0);
    panY = qBound(-1.0, y, 1.0);
    const bool shouldInteract = zoomFactor > 1.0;
    if (interactionEnabled != shouldInteract) {
        interactionEnabled = shouldInteract;
        dragging = false;
    }
    update();
    syncGeometry();
}

bool DetectionOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != inputSurface)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (interactionEnabled && mouseEvent->button() == Qt::LeftButton) {
            dragging = true;
            lastDragPosition = mouseEvent->position().toPoint();
            inputSurface->setCursor(Qt::ClosedHandCursor);
            inputSurface->grabMouse();
            return true;
        }
    }
    if (event->type() == QEvent::MouseMove && dragging) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint currentPosition = mouseEvent->position().toPoint();
        emit dragDelta(currentPosition - lastDragPosition);
        lastDragPosition = currentPosition;
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease && dragging) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            dragging = false;
            inputSurface->releaseMouse();
            inputSurface->setCursor(Qt::OpenHandCursor);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DetectionOverlay::paintEvent(QPaintEvent *)
{
    if (boxes.isEmpty() || srcWidth <= 0 || srcHeight <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const double targetAspect = height() > 0 ? double(width()) / height() : 1.0;
    const double sourceAspect = double(srcWidth) / srcHeight;
    double baseCropWidth = srcWidth;
    double baseCropHeight = srcHeight;
    if (sourceAspect > targetAspect)
        baseCropWidth = srcHeight * targetAspect;
    else
        baseCropHeight = srcWidth / targetAspect;

    const double cropWidth = baseCropWidth / zoomFactor;
    const double cropHeight = baseCropHeight / zoomFactor;
    const double cropLeft = (srcWidth - cropWidth) * (panX + 1.0) / 2.0;
    const double cropTop = (srcHeight - cropHeight) * (panY + 1.0) / 2.0;
    const double scaleX = width() / cropWidth;
    const double scaleY = height() / cropHeight;

    for (const DetectionBox &box : std::as_const(boxes)) {
        QColor color;
        if (box.cls == "FIRE") color = QColor("#f87171");
        else if (box.cls == "PERSON") color = QColor("#38bdf8"); // 화재/연기(빨강/주황)와 구분되는 하늘색
        else color = QColor("#fb923c"); // SMOKE 등
        const QRectF rect((box.x - cropLeft) * scaleX, (box.y - cropTop) * scaleY,
                          box.w * scaleX, box.h * scaleY);

        painter.setPen(QPen(color, 2));
        painter.drawRect(rect);

        const QString label = QString("%1 %2%").arg(box.cls).arg(int(box.score * 100));
        painter.setPen(Qt::white);
        painter.fillRect(QRectF(rect.left(), rect.top() - 16, painter.fontMetrics().horizontalAdvance(label) + 6, 16), color);
        painter.drawText(QPointF(rect.left() + 3, rect.top() - 4), label);
    }
}
