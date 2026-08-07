#include "VideoWidget.h"
#include "DetectionOverlay.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTimer>
#include <QtGlobal>

VideoWidget::VideoWidget(int channel, QWidget *parent)
    : QWidget(parent)
    , channelNumber(channel)
{
    setStyleSheet("background-color:#14141f; border:1px solid #232333; border-radius:8px;");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 8, 10, 8);

    auto *header = new QHBoxLayout;
    header->setSpacing(8);
    titleLabel = new QLabel(QString("Ch.%1").arg(channel), this);
    titleLabel->setStyleSheet("color:#8d87a0; font-size:13px; border:none;");
    header->addWidget(titleLabel);
    header->addStretch();

    alarmBadge = new QLabel("감지", this);
    alarmBadge->setStyleSheet(
        "background-color:#f59e0b; color:#0a0a12; font-size:13px; font-weight:bold; "
        "border-radius:9px; padding:3px 10px; border:none;");
    alarmBadge->setVisible(false);
    header->addWidget(alarmBadge);

    personBadge = new QLabel(this);
    personBadge->setStyleSheet("font-size:16px; font-weight:bold; border:none;");
    header->addWidget(personBadge);

    speakerIcon = new QLabel("🔊", this);
    speakerIcon->setStyleSheet("font-size:16px; border:none;");
    speakerIcon->setVisible(false);
    header->addWidget(speakerIcon);

    auto *liveBadge = new QLabel("● LIVE", this);
    liveBadge->setStyleSheet("color:#f87171; font-size:14px; font-weight:bold; border:none;");
    header->addWidget(liveBadge);

    const QString zoomButtonStyle =
        "QPushButton { color:#c4bfd2; background:#232333; border:1px solid #36364a; "
        "border-radius:4px; font-size:15px; font-weight:bold; }"
        "QPushButton:hover { color:#ffffff; background:#343448; }";
    auto *zoomOutBtn = new QPushButton("−", this);
    zoomOutBtn->setFixedSize(26, 26);
    zoomOutBtn->setToolTip("0.1배 축소");
    zoomOutBtn->setStyleSheet(zoomButtonStyle);
    connect(zoomOutBtn, &QPushButton::clicked, this, [this]() { changeZoom(-1); });
    header->addWidget(zoomOutBtn);

    zoomLabel = new QLabel("1.0x", this);
    zoomLabel->setAlignment(Qt::AlignCenter);
    zoomLabel->setFixedWidth(38);
    zoomLabel->setToolTip("클릭하면 배율과 위치를 초기화");
    zoomLabel->setCursor(Qt::PointingHandCursor);
    zoomLabel->setStyleSheet("color:#c4bfd2; font-size:12px; border:none;");
    zoomLabel->installEventFilter(this);
    header->addWidget(zoomLabel);

    auto *zoomInBtn = new QPushButton("+", this);
    zoomInBtn->setFixedSize(26, 26);
    zoomInBtn->setToolTip("0.1배 확대 (최대 2.5배)");
    zoomInBtn->setStyleSheet(zoomButtonStyle);
    connect(zoomInBtn, &QPushButton::clicked, this, [this]() { changeZoom(1); });
    header->addWidget(zoomInBtn);

    auto *expandBtn = new QPushButton("⛶", this);
    expandBtn->setCursor(Qt::PointingHandCursor);
    expandBtn->setFixedSize(26, 26);
    expandBtn->setToolTip("확대 보기");
    expandBtn->setStyleSheet(
        "QPushButton { color:#8d87a0; background:transparent; border:none; font-size:16px; }"
        "QPushButton:hover { color:#f5f5fa; }");
    connect(expandBtn, &QPushButton::clicked, this, [this]() { emit doubleClicked(channelNumber); });
    header->addWidget(expandBtn);

    layout->addLayout(header);

    // 화면에 보이는 고정 크기 영역. video는 이 안에서 확대/이동하고, 벗어난 부분은 OS가
    // 네이티브 자식 창을 부모 경계로 자동 클리핑해준다(별도 클리핑 코드 불필요).
    videoViewport = new QWidget(this);
    videoViewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoViewport->setStyleSheet("background-color:black;");
    videoViewport->installEventFilter(this); // 리사이즈될 때마다 video 크기/위치 재계산
    layout->addWidget(videoViewport, 1);

    placeholderLabel = new QLabel("연결 중...", this);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet("color:#8d87a0; background-color:#0d0d16; border:none;");
    layout->addWidget(placeholderLabel, 1);

    // libvlc가 이 위젯의 네이티브 HWND에 직접 그림 (Qt가 그 위에 페인트하면 안 됨).
    // videoViewport의 자식으로 두되 레이아웃 관리는 받지 않고 updateVideoTransform()이 직접
    // setGeometry()로 크기/위치를 조절한다 (확대=크게, 이동=위치 이동).
    video = new QWidget(videoViewport);
    video->setAttribute(Qt::WA_NativeWindow);
    video->setAttribute(Qt::WA_PaintOnScreen);
    video->setAttribute(Qt::WA_NoSystemBackground);
    video->setStyleSheet("background-color:black;");
    video->hide();

    // 감지 박스 오버레이: videoViewport(고정 영역)를 따라다니는 독립된 최상위 창.
    // video 자체는 줌/드래그로 계속 움직이므로 오버레이가 그걸 따라다니면 박스 위치 계산이
    // 꼬인다 — 항상 고정된 videoViewport를 기준으로 삼는다.
    overlay = new DetectionOverlay(videoViewport);
    connect(overlay, &DetectionOverlay::dragDelta, this, &VideoWidget::applyPanDelta);

    // video는 네이티브 윈도우라 더블클릭이 부모(this)로 안 올라올 수 있어 직접 감시.
    video->installEventFilter(this);

    updateIndicators(); // 초기 상태(사람 없음, 음성안내 없음) 표시

    blinkTimer = new QTimer(this);
    connect(blinkTimer, &QTimer::timeout, this, [this]() {
        if (!personInDanger && !voiceActive)
            return; // 깜빡일 이유가 없으면 그대로 둔다
        blinkOn = !blinkOn;
        updateIndicators();
    });
    blinkTimer->start(500);
}

VideoWidget::~VideoWidget()
{
    delete overlay; // 최상위 창(부모 없음)이라 Qt가 자동으로 안 지워줌
}

void VideoWidget::setChannelTarget(const QString &target)
{
    channelTarget = target;
    titleLabel->setText(QString("Ch.%1 · %2").arg(channelNumber).arg(target));
}

void VideoWidget::setAlarmActive(bool active)
{
    alarmActive = active;
    alarmBadge->setVisible(active);
    setStyleSheet(active
        ? "background-color:#14141f; border:1px solid #f59e0b; border-radius:8px;"
        : "background-color:#14141f; border:1px solid #232333; border-radius:8px;");
}

void VideoWidget::showPlaceholder(const QString &text)
{
    placeholderLabel->setText(text);
    placeholderLabel->show();
    video->hide();
    overlay->hide();
}

void VideoWidget::showConnected()
{
    placeholderLabel->hide();
    video->show();
    updateVideoTransform();
    overlay->syncGeometry();
}

void VideoWidget::setDetectionBoxes(const QVector<DetectionBox> &boxes, int srcW, int srcH)
{
    overlay->setBoxes(boxes, srcW, srcH);
}

void VideoWidget::setPersonStatus(int count, bool inDangerZone)
{
    personCount = count;
    personInDanger = inDangerZone;
    blinkOn = true; // 상태 바뀌는 즉시 밝은 상태로 보여준 뒤 깜빡임 시작
    updateIndicators();
}

void VideoWidget::setVoiceAnnouncementActive(bool active)
{
    voiceActive = active;
    blinkOn = true;
    updateIndicators();
}

void VideoWidget::updateIndicators()
{
    QString color;
    if (personCount <= 0)
        color = "#6b7280"; // 회색 - 없음
    else if (personInDanger)
        color = blinkOn ? "#f87171" : "#7f1d1d"; // 빨강 깜빡 - 있음 + 위험
    else
        color = "#34d399"; // 초록 - 있음

    personBadge->setText(QString("👤 %1").arg(personCount));
    personBadge->setStyleSheet(QString("color:%1; font-size:16px; font-weight:bold; border:none;").arg(color));

    speakerIcon->setVisible(voiceActive);
    if (voiceActive) {
        speakerIcon->setStyleSheet(QString("font-size:16px; border:none; color:%1;")
                                    .arg(blinkOn ? "#fbbf24" : "#7a5f10"));
    }
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    emit doubleClicked(channelNumber);
    QWidget::mouseDoubleClickEvent(event);
}

bool VideoWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == zoomLabel && event->type() == QEvent::MouseButtonRelease) {
        zoomFactor = 1.0;
        panX = 0.0;
        panY = 0.0;
        zoomLabel->setText("1.0x");
        updateVideoTransform();
        return true;
    }
    if (watched == video && event->type() == QEvent::MouseButtonDblClick) {
        emit doubleClicked(channelNumber);
        return true;
    }
    if (watched == videoViewport && event->type() == QEvent::Resize) {
        // 뷰포트 크기가 바뀌면(창 크기 조절, 확대 다이얼로그 전환 등) 같은 배율/위치라도
        // video의 실제 크기·좌표를 새 뷰포트 기준으로 다시 계산해야 한다.
        updateVideoTransform();
        return false; // 리사이즈 이벤트 자체는 그대로 전달돼야 레이아웃이 정상 동작한다
    }
    return QWidget::eventFilter(watched, event);
}

void VideoWidget::applyPanDelta(const QPoint &delta)
{
    if (zoomFactor <= 1.0)
        return;
    const int viewportW = videoViewport->width();
    const int viewportH = videoViewport->height();
    const int videoW = video->width();
    const int videoH = video->height();
    const int maxOffsetX = qMax(0, (videoW - viewportW) / 2);
    const int maxOffsetY = qMax(0, (videoH - viewportH) / 2);
    const int centerX = (viewportW - videoW) / 2;
    const int centerY = (viewportH - videoH) / 2;

    // 마우스 이동량을 video 위치에 그대로 더한다 — 디코드 파이프라인을 건드리지 않는 순수
    // 위젯 이동이라 커서 아래 화면이 정확히 1:1로 따라온다.
    const int newX = qBound(centerX - maxOffsetX, video->x() + delta.x(), centerX + maxOffsetX);
    const int newY = qBound(centerY - maxOffsetY, video->y() + delta.y(), centerY + maxOffsetY);
    video->move(newX, newY);

    panX = maxOffsetX > 0 ? qBound(-1.0, -double(newX - centerX) / maxOffsetX, 1.0) : 0.0;
    panY = maxOffsetY > 0 ? qBound(-1.0, -double(newY - centerY) / maxOffsetY, 1.0) : 0.0;
    overlay->setViewTransform(zoomFactor, panX, panY);
}

void VideoWidget::changeZoom(int direction)
{
    const int nextStep = qBound(10, qRound(zoomFactor * 10.0) + direction, 25);
    zoomFactor = nextStep / 10.0;
    if (zoomFactor <= 1.0) {
        panX = 0.0;
        panY = 0.0;
    }
    zoomLabel->setText(QString::number(zoomFactor, 'f', 1) + "x");
    updateVideoTransform();
}

void VideoWidget::updateVideoTransform()
{
    const QSize vp = videoViewport->size();
    if (vp.isEmpty())
        return;
    const int videoW = qRound(vp.width() * zoomFactor);
    const int videoH = qRound(vp.height() * zoomFactor);
    const int maxOffsetX = qMax(0, (videoW - vp.width()) / 2);
    const int maxOffsetY = qMax(0, (videoH - vp.height()) / 2);
    const int centerX = (vp.width() - videoW) / 2;
    const int centerY = (vp.height() - videoH) / 2;
    const int x = centerX - qRound(panX * maxOffsetX);
    const int y = centerY - qRound(panY * maxOffsetY);
    video->setGeometry(x, y, videoW, videoH);
    overlay->setViewTransform(zoomFactor, panX, panY);
}
