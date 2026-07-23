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
    void setData(const QVector<double> &values, const QStringList &xLabels);
    void setLineColor(const QColor &color);
    // warningLevel/dangerLevel 이하 0이면 해당 기준선을 표시하지 않음.
    void setThresholds(double warningLevel, double dangerLevel);
    void setUnit(const QString &unit);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_values;
    QStringList m_xLabels;
    QColor m_color{"#8b7cf6"};
    double m_warningLevel = -1;
    double m_dangerLevel = -1;
    QString m_unit;
};

#endif // GASGRAPHWIDGET_H
