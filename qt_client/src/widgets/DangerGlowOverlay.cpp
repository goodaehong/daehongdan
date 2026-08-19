#include "DangerGlowOverlay.h"

#include <QPainter>
#include <QLinearGradient>
#include <QTimer>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
constexpr int kGlowDepth = 90;      // 가장자리에서 안쪽으로 번지는 깊이(px)
constexpr double kMinIntensity = 0.25;
constexpr double kMaxIntensity = 1.0;
constexpr double kIntensityStep = 0.11; // 40ms마다 이만큼 오르내림 -> 긴급하게 빠른 펄스 (한 주기 약 0.55초)
}

DangerGlowOverlay::DangerGlowOverlay(QWidget *followTarget)
    : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
    , target(followTarget)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_NoSystemBackground);

#ifdef Q_OS_WIN
    // Qt의 WA_TransparentForMouseEvents는 top-level 창에서 보장되지 않으므로,
    // 네이티브 핸들을 강제로 만든 뒤 Win32 확장 스타일로 클릭 통과를 확실히 걸어준다.
    winId();
    HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_NOACTIVATE);
#endif

    auto *geometryTimer = new QTimer(this);
    connect(geometryTimer, &QTimer::timeout, this, &DangerGlowOverlay::syncGeometry);
    geometryTimer->start(200);

    auto *pulseTimer = new QTimer(this);
    connect(pulseTimer, &QTimer::timeout, this, [this]() {
        if (!active)
            return;
        intensity += kIntensityStep * direction;
        if (intensity >= kMaxIntensity) {
            intensity = kMaxIntensity;
            direction = -1.0;
        } else if (intensity <= kMinIntensity) {
            intensity = kMinIntensity;
            direction = 1.0;
        }
        update();
    });
    pulseTimer->start(40);
}

void DangerGlowOverlay::setActive(bool value)
{
    if (active == value)
        return;
    active = value;
    if (active) {
        intensity = kMinIntensity;
        direction = 1.0;
        syncGeometry();
    } else {
        hide();
    }
    update();
}

void DangerGlowOverlay::syncGeometry()
{
    if (!active || !target || !target->isVisible()) {
        hide();
        return;
    }
    const QPoint topLeft = target->mapToGlobal(QPoint(0, 0));
    setGeometry(topLeft.x(), topLeft.y(), target->width(), target->height());
    if (!isVisible())
        show();
}

void DangerGlowOverlay::paintEvent(QPaintEvent *)
{
    if (!active)
        return;

    QPainter painter(this);
    const int w = width();
    const int h = height();
    const int alpha = int(180 * intensity);
    const QColor glowColor(248, 113, 113, alpha);
    const QColor clearColor(248, 113, 113, 0);

    QLinearGradient top(0, 0, 0, kGlowDepth);
    top.setColorAt(0, glowColor);
    top.setColorAt(1, clearColor);
    painter.fillRect(QRect(0, 0, w, kGlowDepth), top);

    QLinearGradient bottom(0, h - kGlowDepth, 0, h);
    bottom.setColorAt(0, clearColor);
    bottom.setColorAt(1, glowColor);
    painter.fillRect(QRect(0, h - kGlowDepth, w, kGlowDepth), bottom);

    QLinearGradient left(0, 0, kGlowDepth, 0);
    left.setColorAt(0, glowColor);
    left.setColorAt(1, clearColor);
    painter.fillRect(QRect(0, 0, kGlowDepth, h), left);

    QLinearGradient right(w - kGlowDepth, 0, w, 0);
    right.setColorAt(0, clearColor);
    right.setColorAt(1, glowColor);
    painter.fillRect(QRect(w - kGlowDepth, 0, kGlowDepth, h), right);
}
