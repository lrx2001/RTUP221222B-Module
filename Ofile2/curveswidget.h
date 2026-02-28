#ifndef CURVESWIDGET_H
#define CURVESWIDGET_H

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QRect>
#include "curvedatabuffer.h"
#include "layer.h"
#include <map>

class CurvesWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CurvesWidget(QWidget *parent = nullptr);

    void addLayer(const Layer &layer);
    void ensureCurveInBuffer(int curveKey);
    void addCurve(int curveKey, const QColor &color, const QString &layerName);
    void removeCurve(int curveKey);
    void removeCurveFromDisplay(int curveKey);
    void setCurveColor(int curveKey, const QColor &color);
    void setCurveDisplayName(int curveKey, const QString &name);
    void setCurveUnit(int curveKey, const QString &unit);

    void addSample(const std::vector<std::pair<int, double>> &sampleData,
                   const QDateTime &sampleDate);

    void setCurrentPos(int pos);
    void setWindowStart(int start);
    int windowStart() const { return m_windowStart; }
    int visibleWindowSize() const;
    void setZoom(double xScale, double yScale);
    void setBackgroundColor(const QColor &c);

    const CurveDataBuffer &buffer() const { return m_buffer; }
    double getValueAt(int curveKey, int pos) const;
    QColor getCurveColor(int curveKey) const;
    bool isCurveVisible(int curveKey) const;
    void setCurveVisible(int curveKey, bool visible);
    QRect plotRect() const;

signals:
    void currentValueChanged(int pos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    CurveDataBuffer m_buffer;
    std::map<QString, Layer> m_layers;
    std::map<int, QColor> m_curveColors;
    std::map<int, QString> m_curveDisplayNames;
    std::map<int, QString> m_curveUnits;
    std::map<int, bool>   m_curveVisible;

    double m_xScale = 1.0;
    double m_yScale = 1.0;
    QColor m_canvasBg = Qt::white;
    int m_marginLeft = 50;   // 单条 Y 轴（全局刻度）
    int m_marginBottom = 35;
    int m_marginRight = 15;
    int m_marginTop = 15;

    bool m_zooming = false;
    QPoint m_zoomStart;
    int m_windowStart = 0;

    void drawAxesAndGrid(QPainter &p, const QRect &plotRect);
    void drawCurves(QPainter &p, const QRect &plotRect);
    void drawCursor(QPainter &p, const QRect &plotRect);
    void computeDataYRange(double &outMin, double &outMax) const;
    void computeDataYRangeForLayer(const Layer &layer, double &outMin, double &outMax) const;
};

#endif // CURVESWIDGET_H
