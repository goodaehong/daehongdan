#include "GasGraphWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QFont>
#include <algorithm>
#include <numeric>

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

    const QRect area = rect().adjusted(12, 12, -12, -32);

    painter.fillRect(rect(), QColor("#12121c"));

    if (m_values.size() < 2)
        return;

    // 0을 기준으로 두어야 정상/경고/위험 기준선이 절대값으로 의미가 있다.
    const double minVal = 0.0;
    double maxVal = *std::max_element(m_values.begin(), m_values.end());
    if (m_dangerLevel > 0)
        maxVal = std::max(maxVal, m_dangerLevel);
    maxVal = std::max(maxVal, minVal + 1.0) * 1.15; // 위쪽 여백

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

    // 정상(초록)/경고(노랑)/위험(빨강) 범위 배경 밴드
    if (m_warningLevel > 0 || m_dangerLevel > 0) {
        const double warnY = m_warningLevel > 0 ? yFor(m_warningLevel) : area.top();
        const double dangerY = m_dangerLevel > 0 ? yFor(m_dangerLevel) : area.top();
        painter.fillRect(QRectF(area.left(), warnY, area.width(), area.bottom() - warnY), QColor(52, 211, 153, 20));
        painter.fillRect(QRectF(area.left(), dangerY, area.width(), warnY - dangerY), QColor(251, 191, 36, 22));
        painter.fillRect(QRectF(area.left(), area.top(), area.width(), dangerY - area.top()), QColor(248, 113, 113, 26));

        QPen dashPen(QColor("#6a6478"), 1, Qt::DashLine);
        painter.setPen(dashPen);
        if (m_warningLevel > 0) {
            painter.drawLine(QPointF(area.left(), warnY), QPointF(area.right(), warnY));
            painter.drawText(QRectF(area.left(), warnY - 16, 70, 14), Qt::AlignLeft, "경고 " + QString::number(m_warningLevel, 'f', 0));
        }
        if (m_dangerLevel > 0) {
            painter.drawLine(QPointF(area.left(), dangerY), QPointF(area.right(), dangerY));
            painter.drawText(QRectF(area.left(), dangerY - 16, 70, 14), Qt::AlignLeft, "위험 " + QString::number(m_dangerLevel, 'f', 0));
        }
    }

    QPainterPath linePath;
    linePath.moveTo(pointFor(0));
    for (int i = 1; i < m_values.size(); ++i)
        linePath.lineTo(pointFor(i));

    QPainterPath fillPath = linePath;
    fillPath.lineTo(pointFor(m_values.size() - 1).x(), area.bottom());
    fillPath.lineTo(pointFor(0).x(), area.bottom());
    fillPath.closeSubpath();

    QColor fillTop = m_color;
    fillTop.setAlpha(110);
    QColor fillBottom = m_color;
    fillBottom.setAlpha(0);
    QLinearGradient gradient(0, area.top(), 0, area.bottom());
    gradient.setColorAt(0, fillTop);
    gradient.setColorAt(1, fillBottom);
    painter.fillPath(fillPath, gradient);

    painter.setPen(QPen(m_color, 2));
    painter.drawPath(linePath);

    // 평균값 표시
    const double avg = std::accumulate(m_values.begin(), m_values.end(), 0.0) / m_values.size();
    painter.setPen(QColor("#f5f5fa"));
    QFont avgFont = painter.font();
    avgFont.setBold(true);
    painter.setFont(avgFont);
    painter.drawText(QRectF(area.right() - 110, area.top() + 2, 110, 16), Qt::AlignRight,
                      QString("평균 %1%2").arg(avg, 0, 'f', 1).arg(m_unit));

    painter.setPen(QColor("#6a6478"));
    if (!m_xLabels.isEmpty())
        painter.drawText(QRect(area.left(), area.bottom() + 8, 80, 20), Qt::AlignLeft, m_xLabels.first());
    if (m_xLabels.size() > 1)
        painter.drawText(QRect(area.right() - 80, area.bottom() + 8, 80, 20), Qt::AlignRight, m_xLabels.last());
}
