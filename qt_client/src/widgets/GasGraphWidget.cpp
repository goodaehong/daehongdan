#include "GasGraphWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QFont>
#include <algorithm>
#include <numeric>

namespace {
constexpr int kPanelRadius = 12;
}

GasGraphWidget::GasGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
}

void GasGraphWidget::setData(const QVector<double> &values, const QStringList &xLabels)
{
    m_values = values;
    m_xLabels = xLabels;
    update();
}

void GasGraphWidget::setLineColor(const QColor &color)
{
    m_color = color;
    update();
}

void GasGraphWidget::setThresholds(double warningLevel, double dangerLevel)
{
    m_warningLevel = warningLevel;
    m_dangerLevel = dangerLevel;
    update();
}

void GasGraphWidget::setUnit(const QString &unit)
{
    m_unit = unit;
    update();
}

void GasGraphWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 둥근 패널 배경
    QPainterPath panelPath;
    panelPath.addRoundedRect(rect(), kPanelRadius, kPanelRadius);
    painter.fillPath(panelPath, QColor("#12121c"));

    const QRect area = rect().adjusted(44, 14, -14, -34);

    if (m_values.size() < 2)
        return;

    // 0을 기준으로 두어야 정상/경고/위험 기준선이 절대값으로 의미가 있다.
    const double minVal = 0.0;
    const double dataMax = *std::max_element(m_values.begin(), m_values.end());
    double maxVal = dataMax;
    if (m_dangerLevel > 0) {
        maxVal = std::max(maxVal, m_dangerLevel);
        // 라이터 가스를 센서에 직접 대는 등 순간적으로 수만 ppm까지 스파이크가 튀면 축 전체가
        // 거기 맞춰 늘어나서 평소 구간/기준선이 바닥에 눌려 안 보이게 된다. 위험 임계값의 3배를
        // 축 상한으로 캡하고, 그보다 큰 스파이크는 그래프 위쪽에서 잘려 보이게 둔다
        // (그래도 "확 튀었다"는 건 여전히 화면 위로 뚫고 올라가는 모양으로 보인다).
        maxVal = std::min(maxVal, m_dangerLevel * 3.0);
    }
    maxVal = std::max(maxVal, minVal + 1.0) * 1.15; // 위쪽 여백

    // y축에 깔끔한 숫자가 찍히도록 눈금 상한을 5/10 단위로 올림.
    double niceMax = maxVal;
    if (niceMax <= 10.0) niceMax = std::ceil(niceMax);
    else if (niceMax <= 50.0) niceMax = std::ceil(niceMax / 5.0) * 5.0;
    else niceMax = std::ceil(niceMax / 10.0) * 10.0;
    maxVal = niceMax;

    auto pointFor = [&](int i) {
        const double xRatio = double(i) / double(m_values.size() - 1);
        const double yRatio = (m_values[i] - minVal) / (maxVal - minVal);
        return QPointF(area.left() + xRatio * area.width(),
                        area.bottom() - yRatio * area.height());
    };
    auto yFor = [&](double value) {
        const double yRatio = (value - minVal) / (maxVal - minVal);
        return area.bottom() - yRatio * area.height();
    };

    QFont baseFont = painter.font();
    QFont smallFont = baseFont;
    smallFont.setPointSizeF(baseFont.pointSizeF() * 0.92);

    // y축 눈금(0 / 중간 / 최대) + 은은한 가로 기준선
    painter.setFont(smallFont);
    {
        const QPen gridPen(QColor("#1c1c28"), 1);
        for (double ratio : { 0.0, 0.5, 1.0 }) {
            const double value = minVal + (maxVal - minVal) * ratio;
            const int y = int(area.bottom() - ratio * area.height());
            painter.setPen(gridPen);
            painter.drawLine(QPoint(area.left(), y), QPoint(area.right(), y));
            painter.setPen(QColor("#5a5468"));
            painter.drawText(QRect(0, y - 8, area.left() - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
                              QString::number(value, 'f', 0));
        }
    }

    // 정상/경고/위험 경계선 — 배경을 칠하는 대신 기준선 자체를 해당 색의 가는 점선으로.
    if (m_warningLevel > 0 || m_dangerLevel > 0) {
        const double warnY = m_warningLevel > 0 ? yFor(m_warningLevel) : area.top();
        const double dangerY = m_dangerLevel > 0 ? yFor(m_dangerLevel) : area.top();

        auto drawThresholdLine = [&](double y, const QColor &color, const QString &label) {
            QPen pen(color, 1, Qt::DashLine);
            pen.setDashPattern({ 3, 3 });
            painter.setPen(pen);
            painter.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            painter.setPen(color);
            painter.drawText(QRectF(area.right() - 90, y - 15, 90, 14), Qt::AlignRight, label);
        };
        if (m_warningLevel > 0)
            drawThresholdLine(warnY, QColor("#fbbf24"), "경고 " + QString::number(m_warningLevel, 'f', 0));
        if (m_dangerLevel > 0)
            drawThresholdLine(dangerY, QColor("#f87171"), "위험 " + QString::number(m_dangerLevel, 'f', 0));

        painter.setPen(QColor("#34d399"));
        painter.drawText(QRectF(area.right() - 90, area.bottom() - 15, 90, 14), Qt::AlignRight, "안전 구간");
    }

    // 데이터 라인: 각 구간을 3차 베지어로 부드럽게 이어서 그린다 (꺾이지 않는 곡선).
    QVector<QPointF> pts;
    pts.reserve(m_values.size());
    for (int i = 0; i < m_values.size(); ++i)
        pts.append(pointFor(i));

    QPainterPath linePath;
    linePath.moveTo(pts.first());
    for (int i = 1; i < pts.size(); ++i) {
        const QPointF &p0 = pts[i - 1];
        const QPointF &p1 = pts[i];
        const double dx = (p1.x() - p0.x()) * 0.5;
        linePath.cubicTo(QPointF(p0.x() + dx, p0.y()), QPointF(p1.x() - dx, p1.y()), p1);
    }

    QPainterPath fillPath = linePath;
    fillPath.lineTo(pts.last().x(), area.bottom());
    fillPath.lineTo(pts.first().x(), area.bottom());
    fillPath.closeSubpath();

    // 보라 계열처럼 어두운 배경 위에서 체감 밝기가 낮은 색은 100 alpha로는 거의 안 보여서
    // 색상과 무관하게 충분히 보이도록 상단 alpha를 올림.
    QColor fillTop = m_color;
    fillTop.setAlpha(165);
    QColor fillBottom = m_color;
    fillBottom.setAlpha(0);
    QLinearGradient gradient(0, area.top(), 0, area.bottom());
    gradient.setColorAt(0, fillTop);
    gradient.setColorAt(1, fillBottom);
    painter.setClipPath(panelPath);
    painter.fillPath(fillPath, gradient);

    // 라인에 은은한 글로우를 깐 뒤 본선을 얇게 얹어서 입체감을 준다.
    QColor glow = m_color;
    glow.setAlpha(50);
    painter.setPen(QPen(glow, 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(linePath);
    painter.setPen(QPen(m_color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(linePath);
    painter.setClipping(false);

    // 최신값 지점에 강조 마커
    {
        const QPointF last = pts.last();
        QRadialGradient haloGrad(last, 9);
        QColor halo = m_color;
        halo.setAlpha(90);
        haloGrad.setColorAt(0.0, halo);
        halo.setAlpha(0);
        haloGrad.setColorAt(1.0, halo);
        painter.setPen(Qt::NoPen);
        painter.setBrush(haloGrad);
        painter.drawEllipse(last, 9, 9);

        painter.setBrush(m_color);
        painter.drawEllipse(last, 4, 4);
        painter.setPen(QPen(QColor("#12121c"), 1.5));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(last, 4, 4);
    }

    // 평균값 표시
    const double avg = std::accumulate(m_values.begin(), m_values.end(), 0.0) / m_values.size();
    QFont avgFont = baseFont;
    avgFont.setBold(true);
    painter.setFont(avgFont);
    painter.setPen(QColor("#f5f5fa"));
    painter.drawText(QRectF(area.left(), area.top() - 2, area.width(), 16), Qt::AlignLeft,
                      QString("평균 %1%2").arg(avg, 0, 'f', 1).arg(m_unit));

    painter.setFont(smallFont);
    painter.setPen(QColor("#5a5468"));
    if (!m_xLabels.isEmpty())
        painter.drawText(QRect(area.left(), area.bottom() + 10, 80, 18), Qt::AlignLeft, m_xLabels.first());
    if (m_xLabels.size() > 1)
        painter.drawText(QRect(area.right() - 80, area.bottom() + 10, 80, 18), Qt::AlignRight, m_xLabels.last());
}
