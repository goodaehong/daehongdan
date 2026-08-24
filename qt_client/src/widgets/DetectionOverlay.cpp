#include "DetectionOverlay.h"

#include <QPainter>
#include <QTimer>
#include <QMouseEvent>
#include <QFont>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
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
    // 모달 다이얼로그가 떠 있는 동안에도 마찬가지로 내려둔다(안 그러면 다이얼로그를 덮는다).
    if (!target || !target->isVisible() || modalDialogOpen) {
        hide();
        inputSurface->hide();
        return;
    }
    const QPoint topLeft = target->mapToGlobal(QPoint(0, 0));
    const QRect targetGeometry(topLeft, target->size());
    const bool showingRoi = roiEditMode || (roiVisible && !roiRegionList.isEmpty());
    if (boxes.isEmpty() && !showingRoi) {
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
    const bool shouldInteract = zoomFactor > 1.0 || roiEditMode;
    if (interactionEnabled != shouldInteract) {
        interactionEnabled = shouldInteract;
        dragging = false;
    }
    update();
    syncGeometry();
}

void DetectionOverlay::setRoiEditMode(bool enabled)
{
    roiEditMode = enabled;
    // 편집을 끄면 찍다 만 꼭짓점은 버린다(3개까지만 찍고 나간 경우 등).
    pendingVertices.clear();
    hasHoverPos = false;
    dragPolyIndex = -1;
    dragVertexIndex = -1;
    interactionEnabled = enabled || zoomFactor > 1.0;
    inputSurface->setCursor(enabled ? Qt::CrossCursor : Qt::OpenHandCursor);
    inputSurface->setMouseTracking(enabled); // 버튼을 안 눌러도 다음 변 미리보기가 따라오도록
    update();
    syncGeometry();
}

void DetectionOverlay::setRoiRegions(const QVector<RoiRegion> &regions)
{
    roiRegionList = regions;
    update();
}

void DetectionOverlay::setRoiVisible(bool visible)
{
    roiVisible = visible;
    update();
    syncGeometry(); // 다 숨겨서 그릴 게 없어지면 오버레이 창 자체를 내린다
}

DetectionOverlay::CropTransform DetectionOverlay::currentCropTransform() const
{
    // paintEvent의 감지 박스 크롭 계산과 완전히 동일해야 한다 — 여기가 어긋나면 ROI가 화면에
    // 보이는 위치와 서버가 실제로 잘라내는 위치가 서로 다른 곳을 가리키게 된다(PR #50 버그).
    const double targetAspect = height() > 0 ? double(width()) / height() : 1.0;
    const double sourceAspect = srcHeight > 0 ? double(srcWidth) / srcHeight : targetAspect;
    double baseCropWidth = srcWidth;
    double baseCropHeight = srcHeight;
    if (sourceAspect > targetAspect)
        baseCropWidth = srcHeight * targetAspect;
    else
        baseCropHeight = srcWidth / targetAspect;

    const double cropWidth = baseCropWidth / zoomFactor;
    const double cropHeight = baseCropHeight / zoomFactor;
    CropTransform t;
    t.cropLeft = (srcWidth - cropWidth) * (panX + 1.0) / 2.0;
    t.cropTop = (srcHeight - cropHeight) * (panY + 1.0) / 2.0;
    t.scaleX = cropWidth > 0 ? width() / cropWidth : 1.0;
    t.scaleY = cropHeight > 0 ? height() / cropHeight : 1.0;
    return t;
}

QPointF DetectionOverlay::frameToScreen(const QPointF &frameFraction) const
{
    if (srcWidth <= 0 || srcHeight <= 0) // 감지 메시지를 아직 한 번도 못 받음 — 위젯 크기로 대체
        return QPointF(frameFraction.x() * width(), frameFraction.y() * height());
    const CropTransform t = currentCropTransform();
    const double frameX = frameFraction.x() * srcWidth;
    const double frameY = frameFraction.y() * srcHeight;
    return QPointF((frameX - t.cropLeft) * t.scaleX, (frameY - t.cropTop) * t.scaleY);
}

QPointF DetectionOverlay::screenToFrameFraction(const QPointF &screenPixel) const
{
    if (srcWidth <= 0 || srcHeight <= 0)
        return QPointF(screenPixel.x() / qMax(1, width()), screenPixel.y() / qMax(1, height()));
    const CropTransform t = currentCropTransform();
    const double frameX = screenPixel.x() / t.scaleX + t.cropLeft;
    const double frameY = screenPixel.y() / t.scaleY + t.cropTop;
    return QPointF(frameX / srcWidth, frameY / srcHeight);
}

QPolygonF DetectionOverlay::roiToScreen(const QPolygonF &normalized) const
{
    QPolygonF screenPoly;
    screenPoly.reserve(normalized.size());
    for (const QPointF &p : normalized)
        screenPoly << frameToScreen(p);
    return screenPoly;
}

QRectF DetectionOverlay::roiDeleteBadgeRect(const QPolygonF &screenPoly) const
{
    constexpr double kBadgeSize = 18.0;
    const QRectF bounds = screenPoly.boundingRect();
    return QRectF(bounds.right() - kBadgeSize / 2.0, bounds.top() - kBadgeSize / 2.0,
                  kBadgeSize, kBadgeSize);
}

QRectF DetectionOverlay::roiApplyBadgeRect(const QPolygonF &screenPoly) const
{
    // 폭을 글자 실측이 아니라 고정값으로 두는 이유: 그리기와 클릭 판정이 같은 사각형을 써야 하는데,
    // 문구가 "화재+연기"/"화재만"/"연기만"으로 바뀌면 폭도 같이 흔들려서 클릭이 어긋난다.
    constexpr double kBadgeW = 62.0;
    constexpr double kBadgeH = 18.0;
    const QRectF bounds = screenPoly.boundingRect();
    return QRectF(bounds.left(), bounds.bottom() + 3, kBadgeW, kBadgeH);
}

QRectF DetectionOverlay::roiLabelRect(const QPolygonF &screenPoly) const
{
    // 적용 대상 배지 바로 오른쪽. 이름 길이와 무관하게 클릭 판정이 일정하도록 폭 고정.
    const QRectF applyBadge = roiApplyBadgeRect(screenPoly);
    return QRectF(applyBadge.right() + 4, applyBadge.top(), 140.0, applyBadge.height());
}

bool DetectionOverlay::promptRoiLabel(QString &label)
{
    // QInputDialog를 안 쓴다 — OS 기본(밝은) 팔레트를 써서 다크 화면에서 글씨가 거의 안 보였다.
    // 그리고 오버레이/입력 표면은 always-on-top 최상위 창이라 그대로 두면 모달 다이얼로그를
    // 덮어버려서(ROI 안내 문구·영역선이 다이얼로그 위에 겹쳐 보이고 버튼 클릭도 안 먹었다)
    // 다이얼로그 동안엔 내려둔다. 200ms 타이머(syncGeometry)가 곧바로 다시 띄워버리므로
    // hide()만으로는 부족하고 플래그로 막아야 한다.
    modalDialogOpen = true;
    syncGeometry();

    QDialog dialog(target);
    dialog.setWindowTitle("제외 영역 이름");
    dialog.setStyleSheet("background-color:#14141f;");
    dialog.setMinimumWidth(420);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(28, 24, 28, 24);
    layout->setSpacing(12);

    auto *header = new QLabel("제외 영역 이름", &dialog);
    header->setStyleSheet("color:#f5f5fa; font-size:17px; font-weight:bold; "
                          "font-family:\"hanwhaGothic EL\"; border:none;");
    layout->addWidget(header);

    auto *sub = new QLabel("이 영역이 무엇인지 적어두면, 나중에 지워도 되는 영역인지 판단할 수 있습니다.", &dialog);
    sub->setStyleSheet("color:#8d87a0; font-size:13px; border:none;");
    sub->setWordWrap(true);
    layout->addWidget(sub);

    auto *edit = new QLineEdit(label, &dialog);
    edit->setPlaceholderText("예: 3번 라인 경광등");
    edit->setMinimumHeight(38);
    edit->setStyleSheet(
        "QLineEdit { background-color:#1a1a26; color:#f5f5fa; border:1px solid #232333; "
        "border-radius:6px; padding:8px; font-size:14px; }"
        "QLineEdit:focus { border:1px solid #8b7cf6; }");
    edit->selectAll();
    layout->addWidget(edit);

    layout->addSpacing(6);
    auto *btnRow = new QHBoxLayout;
    btnRow->setSpacing(10);
    auto *cancelBtn = new QPushButton("취소", &dialog);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    cancelBtn->setStyleSheet(
        "QPushButton { background-color:#232333; color:#f5f5fa; font-size:14px; "
        "border:none; border-radius:8px; padding:12px; }"
        "QPushButton:hover { background-color:#2c2c40; }");
    auto *okBtn = new QPushButton("저장", &dialog);
    okBtn->setCursor(Qt::PointingHandCursor);
    okBtn->setDefault(true);
    okBtn->setStyleSheet(
        "QPushButton { background-color:#8b7cf6; color:white; font-weight:bold; "
        "font-family:\"hanwhaGothic EL\"; font-size:14px; border:none; border-radius:8px; padding:12px; }"
        "QPushButton:hover { background-color:#7c6ce8; }");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(okBtn);
    layout->addLayout(btnRow);

    connect(cancelBtn, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(edit, &QLineEdit::returnPressed, &dialog, &QDialog::accept);

    const bool accepted = (dialog.exec() == QDialog::Accepted);
    if (accepted)
        label = edit->text().trimmed();

    modalDialogOpen = false;
    syncGeometry(); // 편집 모드/영역 유무를 다시 보고 알아서 복원한다
    return accepted;
}

QColor DetectionOverlay::roiColorFor(const RoiRegion &region)
{
    // 감지 박스와 같은 색 언어를 쓴다: FIRE=빨강, SMOKE=주황. 둘 다면 어느 쪽도 아닌 보라.
    if (region.applyFire && region.applySmoke) return QColor("#a78bfa");
    if (region.applyFire) return QColor("#f87171");
    return QColor("#fb923c");
}

QString DetectionOverlay::roiApplyText(const RoiRegion &region)
{
    if (region.applyFire && region.applySmoke) return "화재+연기";
    return region.applyFire ? "화재만" : "연기만";
}

void DetectionOverlay::hitTestVertex(const QPointF &pos, int &polyIndex, int &vertexIndex) const
{
    constexpr double kGrabRadius = 9.0;
    // 나중에 만든 영역부터 검사 — 겹쳐 있으면 위에 그려진 것을 집는다.
    for (int i = roiRegionList.size() - 1; i >= 0; --i) {
        const QPolygonF screenPoly = roiToScreen(roiRegionList[i].points);
        for (int v = 0; v < screenPoly.size(); ++v) {
            if (QLineF(screenPoly[v], pos).length() <= kGrabRadius) {
                polyIndex = i;
                vertexIndex = v;
                return;
            }
        }
    }
    polyIndex = -1;
    vertexIndex = -1;
}

bool DetectionOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != inputSurface)
        return QWidget::eventFilter(watched, event);

    if (roiEditMode) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPointF pos = mouseEvent->position();
            if (mouseEvent->button() == Qt::RightButton) {
                // 꼭짓점을 찍는 중이면 마지막 하나만 취소(실수로 잘못 찍었을 때 되돌리기).
                if (!pendingVertices.isEmpty()) {
                    pendingVertices.removeLast();
                    update();
                    return true;
                }
                // 아니면 우클릭한 지점을 포함하는 영역을 지운다(위에 그려진 것 우선).
                for (int i = roiRegionList.size() - 1; i >= 0; --i) {
                    if (roiToScreen(roiRegionList[i].points).containsPoint(pos, Qt::OddEvenFill)) {
                        roiRegionList.remove(i);
                        update();
                        break;
                    }
                }
                return true;
            }
            if (mouseEvent->button() == Qt::LeftButton) {
                if (pendingVertices.isEmpty()) {
                    for (int i = roiRegionList.size() - 1; i >= 0; --i) {
                        const QPolygonF screenPoly = roiToScreen(roiRegionList[i].points);
                        // × 배지를 눌렀으면 새 영역을 시작하지 말고 그 영역을 바로 지운다.
                        if (roiDeleteBadgeRect(screenPoly).contains(pos)) {
                            roiRegionList.remove(i);
                            update();
                            return true;
                        }
                        // 이름 칸: 클릭하면 입력창. 나중에 이 영역이 뭐였는지 알아보게 하는 용도.
                        if (roiLabelRect(screenPoly).contains(pos)) {
                            QString text = roiRegionList[i].label;
                            if (promptRoiLabel(text))
                                roiRegionList[i].label = text;
                            update();
                            return true;
                        }
                        // 적용 대상 배지: 둘다 -> 화재만 -> 연기만 -> 둘다 순으로 순환.
                        if (roiApplyBadgeRect(screenPoly).contains(pos)) {
                            RoiRegion &r = roiRegionList[i];
                            if (r.applyFire && r.applySmoke) {
                                r.applySmoke = false;            // -> 화재만
                            } else if (r.applyFire) {
                                r.applyFire = false;             // -> 연기만
                                r.applySmoke = true;
                            } else {
                                r.applyFire = true;              // -> 둘 다
                            }
                            update();
                            return true;
                        }
                    }
                    // 기존 꼭짓점을 집었으면 새로 찍는 대신 그 점을 끌어서 위치를 미세 조정한다.
                    hitTestVertex(pos, dragPolyIndex, dragVertexIndex);
                    if (dragPolyIndex >= 0) {
                        inputSurface->grabMouse();
                        return true;
                    }
                }
                // 꼭짓점 찍기. 4개가 모이면 영역으로 확정(적용 대상은 기본 "둘 다").
                // 화면 픽셀 그대로가 아니라 원본 프레임 기준 0~1로 변환해서 저장한다 —
                // 편집은 항상 zoom=1.0/pan=0에서 하지만, 크롭(letterbox)은 그 상태에서도 걸려 있다.
                pendingVertices.append(pos);
                if (pendingVertices.size() == 4) {
                    RoiRegion region;
                    for (const QPointF &p : std::as_const(pendingVertices))
                        region.points << screenToFrameFraction(p);
                    roiRegionList.append(region);
                    pendingVertices.clear();
                }
                update();
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPointF pos = mouseEvent->position();
            if (dragPolyIndex >= 0) {
                QPointF frac = screenToFrameFraction(pos);
                roiRegionList[dragPolyIndex].points[dragVertexIndex] =
                    QPointF(qBound(0.0, frac.x(), 1.0), qBound(0.0, frac.y(), 1.0));
                update();
                return true;
            }
            hoverPos = pos;
            hasHoverPos = true;
            if (!pendingVertices.isEmpty())
                update(); // 다음 변이 어디로 이어질지 미리보기
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && dragPolyIndex >= 0) {
            dragPolyIndex = -1;
            dragVertexIndex = -1;
            inputSurface->releaseMouse();
            update();
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

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
    if (boxes.isEmpty() && srcWidth <= 0 && srcHeight <= 0
        && !roiEditMode && !(roiVisible && !roiRegionList.isEmpty()))
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (!boxes.isEmpty() && srcWidth > 0 && srcHeight > 0) {
    const CropTransform t = currentCropTransform();

    for (const DetectionBox &box : std::as_const(boxes)) {
        QColor color;
        if (box.cls == "FIRE") color = QColor("#f87171");
        else if (box.cls == "PERSON") color = QColor("#38bdf8"); // 화재/연기(빨강/주황)와 구분되는 하늘색
        else color = QColor("#fb923c"); // SMOKE 등
        const QRectF rect((box.x - t.cropLeft) * t.scaleX, (box.y - t.cropTop) * t.scaleY,
                          box.w * t.scaleX, box.h * t.scaleY);
        // 전체 화면에 가까운 박스는 영상 크롭 경계와 정확히 겹쳐 네 변과 라벨이
        // 모두 잘릴 수 있다. 실제로 보이는 영역 안쪽에 테두리를 고정한다.
        const QRectF paintBounds = QRectF(this->rect()).adjusted(3, 3, -3, -3);
        const QRectF visibleRect = rect.intersected(paintBounds);
        if (visibleRect.isEmpty())
            continue;

        painter.setPen(QPen(color, 2));
        painter.drawRect(visibleRect);

        const QString label = QString("%1 %2%").arg(box.cls).arg(int(box.score * 100));
        painter.setPen(Qt::white);
        const double labelWidth = painter.fontMetrics().horizontalAdvance(label) + 6;
        const double labelTop = qMax(paintBounds.top(), visibleRect.top() - 16);
        const QRectF labelRect(visibleRect.left(), labelTop, labelWidth, 16);
        painter.fillRect(labelRect.intersected(paintBounds), color);
        painter.drawText(QPointF(visibleRect.left() + 3, labelTop + 12), label);
    }
    }

    // ROI(감시 제외 영역)는 zoom=1.0/pan=0 기준 화면 비율로 저장돼 있어서, 위 감지 박스와 달리
    // srcWidth/srcHeight나 크롭 계산 없이 위젯 크기에 바로 곱해서 그린다.
    if (roiEditMode || (roiVisible && !roiRegionList.isEmpty())) {
        const QColor roiColor("#f87171"); // 새로 찍는 중인 꼭짓점 표시용(확정 전엔 적용 대상이 없음)
        for (int i = 0; i < roiRegionList.size(); ++i) {
            const RoiRegion &region = roiRegionList[i];
            const QPolygonF screenPoly = roiToScreen(region.points);
            const QColor color = roiColorFor(region);

            if (roiEditMode) {
                QColor fill = color;
                fill.setAlpha(60);
                painter.setPen(QPen(color, 2, Qt::DashLine));
                painter.setBrush(fill);
            } else {
                // 평상시엔 영상이 주인공이라 채우기를 아주 옅게만 깐다. 다만 너무 흐리면 영역이
                // 있는지조차 안 보여서(👁 토글이 늘 꺼진 것처럼 느껴짐) 윤곽선은 확실히 남긴다.
                QColor outline = color;
                outline.setAlpha(210);
                QColor fill = color;
                fill.setAlpha(30);
                painter.setPen(QPen(outline, 2, Qt::DashLine));
                painter.setBrush(fill);
            }
            painter.drawPolygon(screenPoly);

            if (roiEditMode) {
                // 꼭짓점 핸들 — 끌어서 조정할 수 있다는 걸 보여주고 실제 클릭 목표가 된다.
                painter.setPen(QPen(Qt::white, 1.5));
                painter.setBrush(color);
                for (const QPointF &v : screenPoly)
                    painter.drawEllipse(v, 4.5, 4.5);

                // 적용 대상 배지(클릭하면 순환) — 어느 감지에 걸리는 영역인지 한눈에 보이게.
                const QRectF applyBadge = roiApplyBadgeRect(screenPoly);
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawRoundedRect(applyBadge, 4, 4);
                QFont badgeFont = painter.font();
                badgeFont.setPixelSize(11);
                painter.setFont(badgeFont);
                painter.setPen(QColor("#1a1a26"));
                painter.drawText(applyBadge, Qt::AlignCenter, roiApplyText(region));

                // 이름 칸 — 비어 있으면 "+ 이름"으로 눌러서 적을 수 있다는 걸 알려준다.
                const QRectF labelRect = roiLabelRect(screenPoly);
                const bool hasLabel = !region.label.isEmpty();
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(26, 26, 38, hasLabel ? 220 : 150));
                painter.drawRoundedRect(labelRect, 4, 4);
                painter.setPen(hasLabel ? QColor("#f5f5fa") : QColor("#8d87a0"));
                painter.drawText(labelRect.adjusted(6, 0, -6, 0), Qt::AlignVCenter | Qt::AlignLeft,
                                  hasLabel ? painter.fontMetrics().elidedText(
                                                 region.label, Qt::ElideRight, int(labelRect.width()) - 12)
                                            : "+ 이름");

                const QRectF badge = roiDeleteBadgeRect(screenPoly);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#1a1a26"));
                painter.drawEllipse(badge);
                painter.setPen(QPen(Qt::white, 2));
                const QPointF c = badge.center();
                const double h = badge.width() * 0.28;
                painter.drawLine(QPointF(c.x() - h, c.y() - h), QPointF(c.x() + h, c.y() + h));
                painter.drawLine(QPointF(c.x() - h, c.y() + h), QPointF(c.x() + h, c.y() - h));
            }
        }

        // 찍고 있는 중인 꼭짓점들 — 지금까지 찍은 변 + 커서까지 이어지는 미리보기 변.
        if (!pendingVertices.isEmpty()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 1.5));
            for (int i = 1; i < pendingVertices.size(); ++i)
                painter.drawLine(pendingVertices[i - 1], pendingVertices[i]);
            if (hasHoverPos) {
                painter.setPen(QPen(QColor(255, 255, 255, 130), 1, Qt::DashLine));
                painter.drawLine(pendingVertices.last(), hoverPos);
                // 마지막 4번째 점을 찍으면 어디서 닫힐지도 같이 보여준다.
                if (pendingVertices.size() == 3)
                    painter.drawLine(hoverPos, pendingVertices.first());
            }
            painter.setPen(QPen(Qt::white, 1.5));
            painter.setBrush(roiColor);
            for (const QPointF &v : std::as_const(pendingVertices))
                painter.drawEllipse(v, 4.5, 4.5);
        }

        if (roiEditMode) {
            // 영상 위쪽(제일 눈길이 먼저 가는 자리)을 가리지 않도록 하단으로 옮겼다. 영상 내용과
            // 무관하게 잘 읽히도록 얇은 반투명 바탕을 깔아준다.
            const QRect hintRect(0, height() - 26, width(), 26);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 140));
            painter.drawRect(hintRect);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(Qt::white);
            QFont hintFont = painter.font();
            hintFont.setPixelSize(12);
            painter.setFont(hintFont);
            const QString hint = pendingVertices.isEmpty()
                ? "클릭: 꼭짓점 찍기(4개) · 꼭짓점 드래그: 위치 조정 · 색 배지 클릭: 적용 대상 변경 · ×(우클릭): 삭제"
                : QString("꼭짓점 %1/4 — 클릭해서 이어 찍으세요 · 우클릭: 마지막 점 취소")
                      .arg(pendingVertices.size());
            painter.drawText(hintRect.adjusted(8, 0, -8, 0), Qt::AlignLeft | Qt::AlignVCenter, hint);
        }
    }
}
