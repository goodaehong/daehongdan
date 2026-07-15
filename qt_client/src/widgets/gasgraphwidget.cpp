#include "gasgraphwidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <algorithm>

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

void GasGraphWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect area = rect().adjusted(12, 12, -12, -32);

    painter.fillRect(rect(), QColor("#12121c"));

    if (m_values.size() < 2)
        return;

    double maxVal = *std::max_element(m_values.begin(), m_values.end());
    double minVal = *std::min_element(m_values.begin(), m_values.end());
    if (maxVal - minVal < 1.0)
        maxVal = minVal + 1.0;

    auto pointFor = [&](int i) {
        const double xRatio = double(i) / double(m_values.size() - 1);
        const double yRatio = (m_values[i] - minVal) / (maxVal - minVal);
        return QPointF(area.left() + xRatio * area.width(),
                        area.bottom() - yRatio * area.height());
    };

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

    painter.setPen(QColor("#6a6478"));
    if (!m_xLabels.isEmpty())
        painter.drawText(QRect(area.left(), area.bottom() + 8, 80, 20), Qt::AlignLeft, m_xLabels.first());
    if (m_xLabels.size() > 1)
        painter.drawText(QRect(area.right() - 80, area.bottom() + 8, 80, 20), Qt::AlignRight, m_xLabels.last());
}
