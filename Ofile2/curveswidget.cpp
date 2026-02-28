#include "curveswidget.h"
#include <algorithm>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QFont>
#include <QFontMetrics>
#include <QtMath>

CurvesWidget::CurvesWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(400, 200);
}

void CurvesWidget::addLayer(const Layer &layer)
{
    m_layers[layer.name] = layer;
    update();
}

void CurvesWidget::ensureCurveInBuffer(int curveKey)
{
    m_buffer.addCurve(curveKey);
    update();
}

void CurvesWidget::addCurve(int curveKey, const QColor &color, const QString &layerName)
{
    if (!m_buffer.addCurve(curveKey))
        return;
    m_curveColors[curveKey] = color;
     m_curveVisible[curveKey] = true;
    if (m_layers.count(layerName)) {
        auto &vec = m_layers[layerName].curveKeys;
        if (std::find(vec.begin(), vec.end(), curveKey) == vec.end())
            vec.push_back(curveKey);
    }
    update();
}

void CurvesWidget::removeCurve(int curveKey)
{
    m_buffer.removeCurve(curveKey);
    m_curveColors.erase(curveKey);
    m_curveDisplayNames.erase(curveKey);
    m_curveUnits.erase(curveKey);
    m_curveVisible.erase(curveKey);
    for (auto &kv : m_layers) {
        auto &vec = kv.second.curveKeys;
        vec.erase(std::remove(vec.begin(), vec.end(), curveKey), vec.end());
    }
    update();
}

void CurvesWidget::removeCurveFromDisplay(int curveKey)
{
    m_curveColors.erase(curveKey);
    m_curveDisplayNames.erase(curveKey);
    m_curveUnits.erase(curveKey);
    m_curveVisible.erase(curveKey);
    for (auto &kv : m_layers) {
        auto &vec = kv.second.curveKeys;
        vec.erase(std::remove(vec.begin(), vec.end(), curveKey), vec.end());
    }
    update();
}

void CurvesWidget::setCurveDisplayName(int curveKey, const QString &name)
{
    m_curveDisplayNames[curveKey] = name;
    update();
}

void CurvesWidget::setCurveUnit(int curveKey, const QString &unit)
{
    m_curveUnits[curveKey] = unit;
    update();
}

void CurvesWidget::setCurveColor(int curveKey, const QColor &color)
{
    if (m_curveColors.count(curveKey))
        m_curveColors[curveKey] = color;
    update();
}

void CurvesWidget::addSample(const std::vector<std::pair<int, double>> &sampleData,
                             const QDateTime &sampleDate)
{
    m_buffer.addSample(sampleData, sampleDate);
    update();
}

void CurvesWidget::setCurrentPos(int pos)
{
    if (pos < 0) pos = 0;
    if (pos >= m_buffer.count) pos = m_buffer.count - 1;
    m_buffer.currentPos = pos;
    emit currentValueChanged(pos);
    update();
}

int CurvesWidget::visibleWindowSize() const
{
    // 视窗长度：最多 300 帧（约 5 分钟，按 1s/帧），不足时显示全部已有数据
    const int kWindowSamples = 300;
    if (m_buffer.count <= 0) return 0;
    if (m_buffer.count <= kWindowSamples)
        return m_buffer.count;                       // 数据少于 5 分钟：一屏显示全部数据
    return qMin(kWindowSamples, CurveDataBuffer::BufferSize); // 数据多于 5 分钟：一屏固定 5 分钟
}

void CurvesWidget::setWindowStart(int start)
{
    int size = visibleWindowSize();
    if (size <= 0) { m_windowStart = 0; update(); return; }
    // 额外预留一段空白区域（例如 5 分钟 = 300 帧），
    // 即使实际数据较少，滚动条仍可右移，右侧为空白
    const int kBlankTailSamples = 300;
    int totalSamples = m_buffer.count + kBlankTailSamples;
    int maxStart = qMax(0, totalSamples - size);
    m_windowStart = qBound(0, start, maxStart);
    update();
}

void CurvesWidget::setZoom(double xScale, double yScale)
{
    m_xScale = xScale;
    m_yScale = yScale;
    update();
}

void CurvesWidget::setBackgroundColor(const QColor &c)
{
    m_canvasBg = c;
    update();
}

double CurvesWidget::getValueAt(int curveKey, int pos) const
{
    auto it = m_buffer.curvesMap.find(curveKey);
    if (it == m_buffer.curvesMap.end()) return 0.0;
    int row = it->second;
    int idx = pos % CurveDataBuffer::BufferSize;
    if (pos < 0 || pos >= m_buffer.count) return 0.0;
    return m_buffer.items[row][idx];
}

QColor CurvesWidget::getCurveColor(int curveKey) const
{
    auto it = m_curveColors.find(curveKey);
    if (it == m_curveColors.end()) return QColor();
    return it->second;
}

bool CurvesWidget::isCurveVisible(int curveKey) const
{
    auto it = m_curveVisible.find(curveKey);
    if (it == m_curveVisible.end()) return true;
    return it->second;
}

void CurvesWidget::setCurveVisible(int curveKey, bool visible)
{
    m_curveVisible[curveKey] = visible;
    update();
}

QRect CurvesWidget::plotRect() const
{
    return rect().adjusted(m_marginLeft, m_marginTop, -m_marginRight, -m_marginBottom);
}

void CurvesWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), m_canvasBg);
    QRect pr = plotRect();
    if (pr.width() <= 0 || pr.height() <= 0) return;
    drawAxesAndGrid(p, pr);
    drawCurves(p, pr);
    drawCursor(p, pr);
}

void CurvesWidget::computeDataYRange(double &outMin, double &outMax) const
{
    outMin = 0.0;
    outMax = 100.0;
    int winSize = visibleWindowSize();
    if (winSize <= 0) return;

    bool first = true;
    for (const auto &layerPair : m_layers) {
        for (int curveKey : layerPair.second.curveKeys) {
            auto it = m_buffer.curvesMap.find(curveKey);
            if (it == m_buffer.curvesMap.end()) continue;
            int row = it->second;
            for (int i = 0; i < winSize; ++i) {
                int logicalPos = m_windowStart + i;
                if (logicalPos < 0 || logicalPos >= m_buffer.count) continue;
                int idx = logicalPos % CurveDataBuffer::BufferSize;
                double v = m_buffer.items[row][idx];
                if (first) { outMin = outMax = v; first = false; }
                else { if (v < outMin) outMin = v; if (v > outMax) outMax = v; }
            }
        }
    }
    if (first) return;
    double range = outMax - outMin;
    if (range < 1e-6) range = 1.0;
    outMin -= range * 0.05;
    outMax += range * 0.05;
}

void CurvesWidget::computeDataYRangeForLayer(const Layer &layer, double &outMin, double &outMax) const
{
    outMin = layer.minScale;
    outMax = layer.maxScale;
    int visibleSamples = qMin(m_buffer.count, CurveDataBuffer::BufferSize);
    if (visibleSamples <= 0) return;

    bool first = true;
    for (int curveKey : layer.curveKeys) {
        auto it = m_buffer.curvesMap.find(curveKey);
        if (it == m_buffer.curvesMap.end()) continue;
        int row = it->second;
        for (int i = 0; i < visibleSamples; ++i) {
            int idx = (m_buffer.count <= CurveDataBuffer::BufferSize)
                     ? i
                     : ((m_buffer.count - visibleSamples + i + CurveDataBuffer::BufferSize) % CurveDataBuffer::BufferSize);
            double v = m_buffer.items[row][idx];
            if (first) { outMin = outMax = v; first = false; }
            else { if (v < outMin) outMin = v; if (v > outMax) outMax = v; }
        }
    }
    if (first) return;
    double range = outMax - outMin;
    if (range < 1e-6) range = 1.0;
    outMin -= range * 0.05;
    outMax += range * 0.05;
}

void CurvesWidget::drawAxesAndGrid(QPainter &p, const QRect &pr)
{
    if (pr.width() <= 0 || pr.height() <= 0) return;

    // 不再绘制网格线，曲线区域为空白背景
    p.setPen(QPen(Qt::black, 1));
    p.drawLine(pr.left(), pr.bottom(), pr.right(), pr.bottom());
    p.drawLine(pr.left(), pr.top(), pr.left(), pr.bottom());

    int winSize = visibleWindowSize();
    if (winSize >= 1) {
        // 底部蓝色时间条
        int timeAxisY = pr.bottom() + 5;
        p.setPen(QPen(Qt::blue, 2, Qt::SolidLine));
        p.drawLine(pr.left(), timeAxisY, pr.right(), timeAxisY);

        // 与 drawCurves 一致的 X 缩放
        double xScale = (winSize <= 1)
                        ? 0.0
                        : (static_cast<double>(pr.width()) / (winSize - 1) / m_xScale);

        p.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));

        // 小刻度：每帧一个；大刻度：每 30 帧（约 30 秒）一个并显示时间
        for (int i = 0; i < winSize; ++i) {
            int logicalPos = m_windowStart + i;
            if (logicalPos < 0 || logicalPos >= m_buffer.count) continue;

            int xCenter;
            if (winSize <= 1 || xScale <= 0.0) {
                xCenter = pr.left() + pr.width() / 2;
            } else {
                xCenter = static_cast<int>(pr.left() + i * xScale);
            }

            bool isMajor = (i % 30 == 0);
            int tickHeight = isMajor ? 8 : 4;
            // 刻度线
            p.drawLine(xCenter, timeAxisY, xCenter, timeAxisY - tickHeight);

            if (isMajor) {
                int physIdx = logicalPos % CurveDataBuffer::BufferSize;
                QString timeStr = m_buffer.sampleTime[physIdx].toString(QStringLiteral("HH:mm:ss"));
                int textWidth = p.fontMetrics().horizontalAdvance(timeStr);
                p.drawText(xCenter - textWidth / 2, timeAxisY + 16, timeStr);
            }
        }
    }

    double yMin = 0.0, yMax = 100.0;
    computeDataYRange(yMin, yMax);
    double range = yMax - yMin;
    if (range < 1e-6) range = 1.0;
    p.setPen(QPen(Qt::black, 1));
    p.setFont(QFont(QStringLiteral("Microsoft YaHei"), 9));
    int axisX = pr.left() - 42;
    for (int i = 0; i <= 5; ++i) {
        double val = yMin + range * i / 5.0;
        int y = pr.bottom() - static_cast<int>((val - yMin) / range * pr.height());
        y = qBound(pr.top(), y, pr.bottom());
        p.drawText(axisX, y + 4, QString::number(val, 'f', 1));
    }
//    p.drawText(axisX, pr.top() - 2, QStringLiteral("值"));
}

void CurvesWidget::drawCurves(QPainter &p, const QRect &pr)
{
    if (m_buffer.count < 1) return;
    if (pr.width() <= 0 || pr.height() <= 0) return;

    int winSize = visibleWindowSize();
    if (winSize <= 0) return;

    double xScale = (winSize <= 1)
                    ? 0.0
                    : (static_cast<double>(pr.width()) / (winSize - 1) / m_xScale);

    double yMin = 0.0, yMax = 100.0;
    computeDataYRange(yMin, yMax);
    double yRange = yMax - yMin;
    if (yRange < 1e-6) yRange = 1.0;

    for (const auto &layerPair : m_layers) {
        const Layer &layer = layerPair.second;
        if (!layer.isVisible) continue;

        for (int curveKey : layer.curveKeys) {
            if (!isCurveVisible(curveKey)) continue;
            auto itRow = m_buffer.curvesMap.find(curveKey);
            if (itRow == m_buffer.curvesMap.end()) continue;
            int row = itRow->second;

            QColor penColor = m_curveColors[curveKey];
            if (!penColor.isValid()) penColor = Qt::red;
            // 比之前略细一些的曲线线宽
            p.setPen(QPen(penColor, 1.2, Qt::SolidLine));

            QPainterPath path;
            bool pathStarted = false;
            for (int i = 0; i < winSize; ++i) {
                int logicalPos = m_windowStart + i;
                if (logicalPos < 0 || logicalPos >= m_buffer.count) continue;
                int idx = logicalPos % CurveDataBuffer::BufferSize;
                double value = m_buffer.items[row][idx];
                double x = (winSize <= 1) ? pr.left() + pr.width() / 2.0 : (pr.left() + i * xScale);
                double norm = (value - yMin) / yRange;
                double y = pr.bottom() - norm * pr.height() * m_yScale - layer.yScrollOffset;
                y = qBound(static_cast<double>(pr.top()), y, static_cast<double>(pr.bottom()));
                QPointF pt(x, y);
                if (!pathStarted) {
                    path.moveTo(pt);
                    pathStarted = true;
                } else {
                    path.lineTo(pt);
                }
            }
            if (pathStarted) {
                if (winSize == 1)
                    p.drawLine(QPointF(pr.left(), path.currentPosition().y()), QPointF(pr.right(), path.currentPosition().y()));
                else
                    p.drawPath(path);
            }
        }
    }
}

void CurvesWidget::drawCursor(QPainter &p, const QRect &pr)
{
    if (m_buffer.count <= 0) return;
    int winSize = visibleWindowSize();
    if (winSize < 1) return;
    double x = pr.left();
    if (winSize > 1) {
        double xScale = static_cast<double>(pr.width()) / (winSize - 1) / m_xScale;
        int visibleIndex = m_buffer.currentPos - m_windowStart;
        visibleIndex = qBound(0, visibleIndex, winSize - 1);
        x = pr.left() + visibleIndex * xScale;
    } else {
        x = pr.left() + pr.width() / 2.0;
    }
    p.setPen(QPen(QColor(0, 200, 0), 2, Qt::SolidLine));
    p.drawLine(QPointF(x, pr.top()), QPointF(x, pr.bottom()));
}

void CurvesWidget::resizeEvent(QResizeEvent *) { update(); }

void CurvesWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_zooming = true;
        m_zoomStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void CurvesWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_zooming) {
        QPoint delta = event->pos() - m_zoomStart;
        // 左拉缩小，右拉放大（与 delta.x 反向）
        double newX = m_xScale - delta.x() / 200.0;
        double newY = m_yScale - delta.y() / 200.0;
        newX = qBound(0.05, newX, 5.0);
        newY = qBound(0.2, newY, 5.0);
        setZoom(newX, newY);
        m_zoomStart = event->pos();
        return;
    }
    QRect pr = plotRect();
    if (pr.width() <= 0 || pr.height() <= 0 || m_buffer.count < 1) return;
    if (!pr.contains(event->pos())) return;

    int winSize = visibleWindowSize();
    if (winSize <= 1) return;

    double xScale = static_cast<double>(pr.width()) / (winSize - 1) / m_xScale;
    if (xScale <= 0) return;
    double visibleIndex = (event->pos().x() - pr.left()) / xScale;
    int pos = qBound(0, static_cast<int>(visibleIndex + 0.5), winSize - 1);
    int actualPos = m_windowStart + pos;
    setCurrentPos(actualPos);
}

void CurvesWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_zooming = false;
        unsetCursor();
    }
}
