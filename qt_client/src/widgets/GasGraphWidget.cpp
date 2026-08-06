#include "GasGraphWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>
#include <numeric>

namespace {
constexpr int kPanelRadius = 12;
// 여백: 왼쪽(y축 눈금 숫자) / 위(평균값 + AVG·MAX 범례 2줄, 전부 오른쪽 위에 쌓임) / 오른쪽 / 아래(x축 시각 라벨).
constexpr int kMarginLeft = 56;
constexpr int kMarginTop = 58;
constexpr int kMarginRight = 16;
constexpr int kMarginBottom = 40;
}

GasGraphWidget::GasGraphWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(220);
    setMouseTracking(true); // 클릭 없이 움직이기만 해도 mouseMoveEvent가 오도록
    // 기본 QToolTip 배색이 검은 글씨라 배경도 어두운 이 화면에서는 거의 안 보였음 -> 직접 지정.
    setStyleSheet(
        "QToolTip { color:#f5f5fa; background-color:#1e1e2c; border:1px solid #3a3a4a; "
        "padding:6px 8px; font-size:13px; }");
}

void GasGraphWidget::setData(const QVector<double> &values, const QStringList &xLabels)
{
    setSeries(values, {}, xLabels);
}

void GasGraphWidget::setSeries(const QVector<double> &avgValues, const QVector<double> &maxValues, const QStringList &xLabels)
{
    m_values = avgValues;
    // 크기가 안 맞으면(방어적으로) max 선은 그리지 않는다.
    m_maxValues = (maxValues.size() == avgValues.size()) ? maxValues : QVector<double>();
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

QRect GasGraphWidget::plotArea() const
{
    return rect().adjusted(kMarginLeft, kMarginTop, -kMarginRight, -kMarginBottom);
}

void GasGraphWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 둥근 패널 배경
    QPainterPath panelPath;
    panelPath.addRoundedRect(rect(), kPanelRadius, kPanelRadius);
    painter.fillPath(panelPath, QColor("#12121c"));

    const QRect area = plotArea();

    if (m_values.size() < 2)
        return;

    // 0을 기준으로 두어야 정상/경고/위험 기준선이 절대값으로 의미가 있다.
    const double minVal = 0.0;
    double dataMax = *std::max_element(m_values.begin(), m_values.end());
    if (!m_maxValues.isEmpty())
        dataMax = std::max(dataMax, *std::max_element(m_maxValues.begin(), m_maxValues.end()));
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

    auto pointForSeries = [&](const QVector<double> &series, int i) {
        const double xRatio = double(i) / double(series.size() - 1);
        const double yRatio = (series[i] - minVal) / (maxVal - minVal);
        return QPointF(area.left() + xRatio * area.width(),
                        area.bottom() - yRatio * area.height());
    };
    auto pointFor = [&](int i) { return pointForSeries(m_values, i); };
    auto yFor = [&](double value) {
        const double yRatio = (value - minVal) / (maxVal - minVal);
        return area.bottom() - yRatio * area.height();
    };

    // 폰트: 전체적으로 확실히 읽히는 크기로. px 고정이라 화면 배율과 무관하게 일관됨.
    QFont baseFont = painter.font();
    QFont axisFont = baseFont;
    axisFont.setPixelSize(13);
    QFont avgLabelFont = baseFont;
    avgLabelFont.setPixelSize(16);
    avgLabelFont.setBold(true);
    QFont legendFont = baseFont;
    legendFont.setPixelSize(12);

    const QString axisTextColor = "#a8a2ba"; // 기존 #5a5468는 너무 어두워서 잘 안 보였음

    // y축 눈금(0 / 중간 / 최대) + 은은한 가로 기준선
    painter.setFont(axisFont);
    {
        const QPen gridPen(QColor("#1c1c28"), 1);
        for (double ratio : { 0.0, 0.5, 1.0 }) {
            const double value = minVal + (maxVal - minVal) * ratio;
            const int y = int(area.bottom() - ratio * area.height());
            painter.setPen(gridPen);
            painter.drawLine(QPoint(area.left(), y), QPoint(area.right(), y));
            painter.setPen(QColor(axisTextColor));
            painter.drawText(QRect(0, y - 9, area.left() - 10, 18), Qt::AlignRight | Qt::AlignVCenter,
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
            painter.drawText(QRectF(area.right() - 100, y - 17, 100, 16), Qt::AlignRight, label);
        };
        if (m_warningLevel > 0)
            drawThresholdLine(warnY, QColor("#fbbf24"), "경고 " + QString::number(m_warningLevel, 'f', 0));
        if (m_dangerLevel > 0)
            drawThresholdLine(dangerY, QColor("#f87171"), "위험 " + QString::number(m_dangerLevel, 'f', 0));

        painter.setPen(QColor("#34d399"));
        painter.drawText(QRectF(area.right() - 100, area.bottom() - 17, 100, 16), Qt::AlignRight, "안전 구간");
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

    // MAX 선(구간 집계 기간에서만 옴): 채우기 없이 얇은 점선으로, AVG보다 밝은 색으로 구분.
    const QColor maxColor = m_color.lighter(170);
    if (!m_maxValues.isEmpty()) {
        QVector<QPointF> maxPts;
        maxPts.reserve(m_maxValues.size());
        for (int i = 0; i < m_maxValues.size(); ++i)
            maxPts.append(pointForSeries(m_maxValues, i));

        QPainterPath maxPath;
        maxPath.moveTo(maxPts.first());
        for (int i = 1; i < maxPts.size(); ++i) {
            const QPointF &p0 = maxPts[i - 1];
            const QPointF &p1 = maxPts[i];
            const double dx = (p1.x() - p0.x()) * 0.5;
            maxPath.cubicTo(QPointF(p0.x() + dx, p0.y()), QPointF(p1.x() - dx, p1.y()), p1);
        }

        QPen maxPen(maxColor, 1.8, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(maxPen);
        painter.drawPath(maxPath);
    }

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

    // 평균값 표시 + AVG/MAX 범례(있을 때만) — 전부 오른쪽 위에 위에서 아래로 쌓아서 표시.
    const double avg = std::accumulate(m_values.begin(), m_values.end(), 0.0) / m_values.size();
    const int avgRowH = 22;
    painter.setFont(avgLabelFont);
    painter.setPen(QColor("#f5f5fa"));
    painter.drawText(QRect(area.left(), 0, area.width(), avgRowH), Qt::AlignRight | Qt::AlignVCenter,
                      QString("평균 %1%2").arg(avg, 0, 'f', 1).arg(m_unit));

    // 텍스트 폭을 실측(QFontMetrics)해서 위젯 오른쪽 끝을 넘어가 잘리지 않게 계산한다.
    if (!m_maxValues.isEmpty()) {
        painter.setFont(legendFont);
        const QFontMetrics fm(legendFont);
        const int textW = std::max(fm.horizontalAdvance("AVG"), fm.horizontalAdvance("MAX"));
        const int swatchW = 16;
        const int gap = 5;
        const int rowH = 17;
        const int lx = area.right() - (swatchW + gap + textW);
        int ly = avgRowH;

        painter.setPen(QPen(m_color, 2.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(lx, ly + rowH / 2, lx + swatchW, ly + rowH / 2);
        painter.setPen(QColor("#d8d4e8"));
        painter.drawText(QRect(lx + swatchW + gap, ly, textW + 2, rowH), Qt::AlignLeft | Qt::AlignVCenter, "AVG");

        ly += rowH;
        painter.setPen(QPen(maxColor, 1.8, Qt::DashLine, Qt::RoundCap));
        painter.drawLine(lx, ly + rowH / 2, lx + swatchW, ly + rowH / 2);
        painter.setPen(QColor("#d8d4e8"));
        painter.drawText(QRect(lx + swatchW + gap, ly, textW + 2, rowH), Qt::AlignLeft | Qt::AlignVCenter, "MAX");
    }

    painter.setFont(axisFont);
    painter.setPen(QColor(axisTextColor));
    if (!m_xLabels.isEmpty())
        painter.drawText(QRect(area.left(), area.bottom() + 12, 100, 20), Qt::AlignLeft, m_xLabels.first());
    if (m_xLabels.size() > 1)
        painter.drawText(QRect(area.right() - 100, area.bottom() + 12, 100, 20), Qt::AlignRight, m_xLabels.last());

    // 마우스가 올라가 있는 지점 강조: 세로 가이드라인 + 해당 포인트를 더 크게 다시 그림.
    if (m_hoverIndex >= 0 && m_hoverIndex < pts.size()) {
        QPen guidePen(QColor(255, 255, 255, 70), 1, Qt::DashLine);
        painter.setPen(guidePen);
        painter.drawLine(QPointF(pts[m_hoverIndex].x(), area.top()), QPointF(pts[m_hoverIndex].x(), area.bottom()));

        painter.setPen(QPen(Qt::white, 2));
        painter.setBrush(m_color);
        painter.drawEllipse(pts[m_hoverIndex], 6, 6);

        if (!m_maxValues.isEmpty()) {
            const QPointF hoverMax = pointForSeries(m_maxValues, m_hoverIndex);
            painter.setPen(QPen(Qt::white, 2));
            painter.setBrush(maxColor);
            painter.drawEllipse(hoverMax, 5, 5);
        }
    }
}

void GasGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);

    if (m_values.size() < 2) {
        QToolTip::hideText();
        return;
    }

    const QRect area = plotArea();
    const QPoint pos = event->pos();
    if (!area.contains(pos)) {
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
        QToolTip::hideText();
        return;
    }

    const double ratio = qBound(0.0, double(pos.x() - area.left()) / double(area.width()), 1.0);
    int index = qRound(ratio * (m_values.size() - 1));
    index = qBound(0, index, m_values.size() - 1);

    if (index != m_hoverIndex) {
        m_hoverIndex = index;
        update(); // 강조 마커/가이드라인을 다시 그리기 위해 리페인트
    }

    // 포인트별 라벨을 다 받은 경우에만 정확한 시각을 보여준다(2개만 받은 옛 호출부는 값만 표시).
    const bool hasPerPointLabels = m_xLabels.size() == m_values.size();
    QString text = hasPerPointLabels ? m_xLabels[index] + "\n" : QString();
    text += QString("평균 %1%2").arg(m_values[index], 0, 'f', 1).arg(m_unit);
    int lineCount = hasPerPointLabels ? 2 : 1;
    if (!m_maxValues.isEmpty()) {
        text += QString("\n최댓값 %1%2").arg(m_maxValues[index], 0, 'f', 1).arg(m_unit);
        ++lineCount;
    }

    // 커서 오른쪽 아래 대신 오른쪽 위로 뜨도록, 툴팁 예상 높이만큼 위로 밀어 올린다.
    const int estimatedHeight = lineCount * 18 + 16;
    const QPoint tooltipPos = event->globalPosition().toPoint() + QPoint(16, -(estimatedHeight + 10));
    QToolTip::showText(tooltipPos, text, this);
}

void GasGraphWidget::leaveEvent(QEvent *event)
{
    if (m_hoverIndex != -1) {
        m_hoverIndex = -1;
        update();
    }
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}
