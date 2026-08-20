#ifndef GASGRAPHWIDGET_H
#define GASGRAPHWIDGET_H

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <QColor>

// 그래프 위에 겹쳐 그리는 사건 마커(경고/위험 전환 시점). x 위치를 시각이 아니라 0.0~1.0 비율로
// 받는 이유는, 실제 표본 시각(rows의 t)을 아는 쪽은 GraphPage라서 변환도 거기서 하는 게 맞아서다.
struct GraphEventMarker {
    double xRatio = 0.0;  // 0.0=그래프 왼쪽 끝, 1.0=오른쪽 끝
    bool danger = false;  // true=위험(빨강) / false=경고(노랑)
    QString label;        // 마커에 마우스 올렸을 때 뜨는 문구
};

class GasGraphWidget : public QWidget
{
    Q_OBJECT

public:
    explicit GasGraphWidget(QWidget *parent = nullptr);
    // 단일 선(원본 데이터, 10분/1시간 기간 등). 내부적으로 setSeries(values, {}, xLabels)와 동일.
    // xLabels를 값 개수만큼(포인트별로) 넘기면 마우스오버 시 정확한 시각이 뜬다. 2개(처음/끝)만 넘겨도
    // 그래프 양끝 라벨은 정상 표시되지만, 마우스오버 시각 표시는 값만 나온다.
    void setData(const QVector<double> &values, const QStringList &xLabels);
    // avg/max 두 선(구간 집계, 6시간/하루 기간 등). max가 비어있으면 단일 선으로 그려진다.
    void setSeries(const QVector<double> &avgValues, const QVector<double> &maxValues, const QStringList &xLabels);
    void setLineColor(const QColor &color);
    // warningLevel/dangerLevel 이하 0이면 해당 기준선을 표시하지 않음.
    void setThresholds(double warningLevel, double dangerLevel);
    void setUnit(const QString &unit);
    // 경고/위험이 발생했던 시점을 그래프 위에 세로선으로 겹쳐 표시한다. 빈 목록이면 아무것도 안 그림.
    void setEventMarkers(const QVector<GraphEventMarker> &markers);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect plotArea() const;

    QVector<double> m_values;
    QVector<double> m_maxValues; // 비어있으면 단일 선 모드
    QStringList m_xLabels;
    QColor m_color{"#8b7cf6"};
    double m_warningLevel = -1;
    double m_dangerLevel = -1;
    QString m_unit;
    int m_hoverIndex = -1; // 마우스가 올라가 있는 포인트 인덱스. -1이면 강조 없음.
    QVector<GraphEventMarker> m_markers;
    // 사건 마커 위에 마우스가 있으면 데이터 포인트 대신 이쪽 문구를 띄운다(둘이 겹치면 마커 우선).
    int m_hoverMarker = -1;
};

#endif // GASGRAPHWIDGET_H
