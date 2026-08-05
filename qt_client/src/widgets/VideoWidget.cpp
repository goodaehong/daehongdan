#include "VideoWidget.h"
#include "DetectionOverlay.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QTimer>

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

    // libvlc가 이 위젯의 네이티브 HWND에 직접 그림 (Qt가 그 위에 페인트하면 안 됨).
    video = new QWidget(this);
    video->setAttribute(Qt::WA_NativeWindow);
    video->setAttribute(Qt::WA_PaintOnScreen);
    video->setAttribute(Qt::WA_NoSystemBackground);
    video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    video->setStyleSheet("background-color:black;");
    layout->addWidget(video, 1);

    placeholderLabel = new QLabel("연결 중...", this);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setStyleSheet("color:#8d87a0; background-color:#0d0d16; border:none;");
    layout->addWidget(placeholderLabel, 1);

    video->hide();

    // 감지 박스 오버레이: video와 같은 부모에 두지 않고 독립된 최상위 창으로 추적시킴.
    overlay = new DetectionOverlay(video);

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
    if (watched == video && event->type() == QEvent::MouseButtonDblClick) {
        emit doubleClicked(channelNumber);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
