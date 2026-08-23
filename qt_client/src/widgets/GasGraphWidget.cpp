#include "GasGraphWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QFont>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QDateTime>
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
}

void GasGraphWidget::setData(const QVector<double> &values, const QStringList &xLabels)
{
    setSeries(values, {}, xLabels);
}

void GasGraphWidget::setSeries(const QVector<double> &avgValues, const QVector<double> &maxValues, const QStringList &xLabels,
                                const QVector<qint64> &sampleTimesSecs, qint64 rangeFromSecs, qint64 rangeToSecs)
{
    m_values = avgValues;
    // 크기가 안 맞으면(방어적으로) max 선은 그리지 않는다.
    m_maxValues = (maxValues.size() == avgValues.size()) ? maxValues : QVector<double>();
    m_xLabels = xLabels;
    m_sampleTimes = (sampleTimesSecs.size() == avgValues.size()) ? sampleTimesSecs : QVector<qint64>();
    m_hasRange = !m_sampleTimes.isEmpty() && rangeToSecs > rangeFromSecs;
    m_rangeFrom = rangeFromSecs;
    m_rangeTo = rangeToSecs;
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

void GasGraphWidget::setEventMarkers(const QVector<GraphEventMarker> &markers)
{
    m_markers = markers;
    m_hoverMarker = -1;
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
        // 시간 범위가 있으면(정각 정렬 구간) 인덱스 균등이 아니라 실제 경과 시간 비율로 배치한다 —
        // 구간이 아직 안 끝났으면(예: 14:20~14:30인데 지금 14:23) 마지막 표본 오른쪽이 비어 보인다.
        double xRatio;
        if (m_hasRange && m_sampleTimes.size() == series.size())
            xRatio = qBound(0.0, double(m_sampleTimes[i] - m_rangeFrom) / double(m_rangeTo - m_rangeFrom), 1.0);
        else
            xRatio = double(i) / double(series.size() - 1);
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
    // L/EL은 서로 다른 family로 등록돼 있어 setBold()만으로는 Qt가 EL을 자동으로 골라주지
    // 않는다 — 페이지들의 QSS와 동일하게 굵은 글씨 자리엔 EL을 직접 지정한다.
    avgLabelFont.setFamily("hanwhaGothic EL");
    QFont legendFont = baseFont;
    legendFont.setPixelSize(13);

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

    // 사건 마커(경고/위험 전환 시점): 세로선 + 상단 깃발. 데이터 라인 위에 얹되 알파를 낮춰서
    // 추이 자체를 가리지 않게 하고, 마우스를 올린 마커만 진하게 강조한다.
    for (int i = 0; i < m_markers.size(); ++i) {
        const GraphEventMarker &m = m_markers[i];
        const double mx = area.left() + qBound(0.0, m.xRatio, 1.0) * area.width();
        const bool hovered = (i == m_hoverMarker);
        QColor mc(m.danger ? "#f87171" : "#fbbf24");

        QColor lineColor = mc;
        lineColor.setAlpha(hovered ? 235 : 120);
        QPen markerPen(lineColor, hovered ? 2.0 : 1.2, Qt::DashLine);
        markerPen.setDashPattern({ 2, 4 });
        painter.setPen(markerPen);
        painter.drawLine(QPointF(mx, area.top()), QPointF(mx, area.bottom()));

        // 상단 깃발(아래를 가리키는 삼각형) — 세로선만으로는 임계선 점선과 헷갈려서 표식을 따로 둔다.
        const qreal fw = hovered ? 5.5 : 4.5;
        QPainterPath flag;
        flag.moveTo(mx - fw, area.top() - 7);
        flag.lineTo(mx + fw, area.top() - 7);
        flag.lineTo(mx, area.top() + 1);
        flag.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.setBrush(mc);
        painter.drawPath(flag);
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
    if (m_hasRange) {
        // 마지막 표본이 아니라 구간 경계 자체(예: 14:20~14:30)를 양 끝 라벨로 쓴다 — 표본이
        // 구간을 다 못 채웠어도(진행 중인 실시간 구간) 축은 항상 구간 전체를 나타내야 한다.
        painter.drawText(QRect(area.left(), area.bottom() + 12, 100, 20), Qt::AlignLeft,
                          QDateTime::fromSecsSinceEpoch(m_rangeFrom).toString("HH:mm"));
        painter.drawText(QRect(area.right() - 100, area.bottom() + 12, 100, 20), Qt::AlignRight,
                          QDateTime::fromSecsSinceEpoch(m_rangeTo).toString("HH:mm"));
    } else {
        if (!m_xLabels.isEmpty())
            painter.drawText(QRect(area.left(), area.bottom() + 12, 100, 20), Qt::AlignLeft, m_xLabels.first());
        if (m_xLabels.size() > 1)
            painter.drawText(QRect(area.right() - 100, area.bottom() + 12, 100, 20), Qt::AlignRight, m_xLabels.last());
    }

    // 값 박스는 데이터 포인트 hover와 사건 마커 hover 양쪽에서 같은 모양으로 쓰므로 람다로 뺀다.
    auto drawTip = [&](const QStringList &lines, const QPointF &anchor) {
        QFont tipFont = painter.font();
        tipFont.setPixelSize(13);
        painter.setFont(tipFont);
        const QFontMetrics tipFm(tipFont);
        int tipW = 0;
        for (const QString &line : lines)
            tipW = std::max(tipW, tipFm.horizontalAdvance(line));
        tipW += 16;
        const int tipLineH = tipFm.height() + 4;
        const int tipH = tipLineH * lines.size() + 8;

        // 커서(포인트) 오른쪽 위로 띄우되, 위젯 경계를 넘어가면 반대쪽으로 붙인다.
        qreal tipX = anchor.x() + 12;
        qreal tipY = anchor.y() - tipH - 12;
        if (tipX + tipW > width() - 4)
            tipX = anchor.x() - tipW - 12;
        if (tipY < 4)
            tipY = anchor.y() + 12;
        const QRectF tipRect(tipX, tipY, tipW, tipH);

        painter.setPen(QPen(QColor("#3a3a4a"), 1));
        painter.setBrush(QColor(30, 30, 44, 235));
        painter.drawRoundedRect(tipRect, 6, 6);

        painter.setPen(QColor("#f5f5fa"));
        for (int i = 0; i < lines.size(); ++i) {
            painter.drawText(QRectF(tipRect.left() + 8, tipRect.top() + 4 + i * tipLineH, tipW - 16, tipLineH),
                              Qt::AlignVCenter | Qt::AlignLeft, lines[i]);
        }
    };

    // 사건 마커에 마우스를 올린 경우 — 그 마커 문구를 그래프 위쪽에 띄운다.
    if (m_hoverMarker >= 0 && m_hoverMarker < m_markers.size()) {
        const GraphEventMarker &m = m_markers[m_hoverMarker];
        const double mx = area.left() + qBound(0.0, m.xRatio, 1.0) * area.width();
        drawTip({ m.label }, QPointF(mx, area.top() + 18));
    }

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

        // 값 박스도 OS 툴팁(QToolTip) 대신 직접 그린다. QToolTip::showText()를 마우스 이동마다
        // 계속 호출하면 마우스를 빠르게 움직였을 때 위치 갱신이 못 따라와서 깨져 보이는 문제가 있었음
        // — paintEvent 안에서 그리면 다른 그래프 요소들처럼 항상 정상적으로 리페인트된다.
        const bool hasPerPointLabels = m_xLabels.size() == m_values.size();
        QStringList tipLines;
        if (hasPerPointLabels)
            tipLines << m_xLabels[m_hoverIndex];
        // 구간 집계(AVG+MAX 두 선) 기간에서만 m_values가 실제 "평균"이다. 원본(단일 선) 기간에는
        // 그 시각에 측정된 값 그대로라 "평균"이라고 하면 틀린 말이 된다 — 그때는 라벨 없이 값만.
        if (m_maxValues.isEmpty()) {
            tipLines << QString("%1%2").arg(m_values[m_hoverIndex], 0, 'f', 1).arg(m_unit);
        } else {
            tipLines << QString("평균 %1%2").arg(m_values[m_hoverIndex], 0, 'f', 1).arg(m_unit);
            tipLines << QString("최댓값 %1%2").arg(m_maxValues[m_hoverIndex], 0, 'f', 1).arg(m_unit);
        }

        drawTip(tipLines, pts[m_hoverIndex]);
    }
}

void GasGraphWidget::mouseMoveEvent(QMouseEvent *event)
{
    QWidget::mouseMoveEvent(event);

    if (m_values.size() < 2) {
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
        return;
    }

    const QRect area = plotArea();
    const QPoint pos = event->pos();

    // 사건 마커를 먼저 본다 — 세로선/깃발 위에 있으면 데이터 포인트 대신 마커 문구를 띄운다.
    // 깃발이 그래프 영역 살짝 위(area.top()-7)에 있어서 y 허용 범위를 그만큼 넓힌다.
    int markerHit = -1;
    if (pos.y() >= area.top() - 10 && pos.y() <= area.bottom()) {
        for (int i = 0; i < m_markers.size(); ++i) {
            const double mx = area.left() + qBound(0.0, m_markers[i].xRatio, 1.0) * area.width();
            if (std::abs(double(pos.x()) - mx) <= 6.0) {
                markerHit = i;
                break;
            }
        }
    }
    if (markerHit != m_hoverMarker) {
        m_hoverMarker = markerHit;
        update();
    }
    if (markerHit >= 0) {
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
        return;
    }

    if (!area.contains(pos)) {
        if (m_hoverIndex != -1) {
            m_hoverIndex = -1;
            update();
        }
        return;
    }

    const double ratio = qBound(0.0, double(pos.x() - area.left()) / double(area.width()), 1.0);
    int index;
    if (m_hasRange && m_sampleTimes.size() == m_values.size()) {
        // 점들이 더 이상 인덱스 균등 간격이 아니므로(시간 비율 배치), 마우스 x를 실제 시각으로
        // 환산한 뒤 가장 가까운 표본을 찾는다. 표본 수가 많아야 수백 개 수준이라 선형 탐색으로 충분.
        const qint64 targetTime = m_rangeFrom + qint64(ratio * double(m_rangeTo - m_rangeFrom));
        qint64 bestDiff = -1;
        index = 0;
        for (int i = 0; i < m_sampleTimes.size(); ++i) {
            const qint64 diff = std::abs(m_sampleTimes[i] - targetTime);
            if (bestDiff < 0 || diff < bestDiff) {
                bestDiff = diff;
                index = i;
            }
        }
    } else {
        index = qRound(ratio * (m_values.size() - 1));
    }
    index = qBound(0, index, m_values.size() - 1);

    if (index != m_hoverIndex) {
        m_hoverIndex = index;
        update(); // 강조 마커/가이드라인/값 박스를 paintEvent에서 다시 그림
    }
}

void GasGraphWidget::leaveEvent(QEvent *event)
{
    if (m_hoverIndex != -1 || m_hoverMarker != -1) {
        m_hoverIndex = -1;
        m_hoverMarker = -1;
        update();
    }
    QWidget::leaveEvent(event);
}
