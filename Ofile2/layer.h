#ifndef LAYER_H
#define LAYER_H

#include <QColor>
#include <QString>
#include <vector>

struct Layer
{
    QString name;
    QString axisLabel;   // Y 轴标签，如 "Bool/A V/Pluse"、"500"、"°C"
    QColor background  = QColor(100, 100, 50, 50);
    QColor gridline    = QColor(255, 255, 0, 0);
    QColor foreground  = QColor(0, 0, 255);
    bool   hasBackground = true;
    bool   hasGridline   = true;
    bool   isVisible     = true;
    double maxScale      = 1000.0;
    double minScale      = -100.0;
    double scaleStep     = 50.0;
    double yScrollOffset = 0.0;
    std::vector<int> curveKeys;
};

#endif // LAYER_H
