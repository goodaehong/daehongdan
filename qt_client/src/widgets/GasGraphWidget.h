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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QVector<double> m_values;
    QStringList m_xLabels;
    QColor m_color{"#8b7cf6"};
};

#endif // GASGRAPHWIDGET_H
