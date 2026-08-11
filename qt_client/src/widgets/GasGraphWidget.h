#ifndef GASGRAPHWIDGET_H
#define GASGRAPHWIDGET_H

#include <QWidget>
#include <QVector>
#include <QStringList>
#include <QColor>

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
};

#endif // GASGRAPHWIDGET_H
