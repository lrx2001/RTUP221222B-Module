#ifndef CURVEDATABUFFER_H
#define CURVEDATABUFFER_H

#include <vector>
#include <map>
#include <QDateTime>

class CurveDataBuffer
{
public:
    // 168 小时 @ 1 次/秒 = 604800 点
    static const int BufferSize = 168 * 3600;
    static const int CurveCount = 256;

    std::map<int, int> curvesMap;   // curveKey -> row index
    std::vector<std::vector<double>> items;
    std::vector<QDateTime> sampleTime;

    int currentPos = 0;
    int count = 0;
    bool isOverlapped = false;

    CurveDataBuffer();

    bool addCurve(int curveKey);
    void removeCurve(int curveKey);
    void resetCurveData();
    void addSample(const std::vector<std::pair<int, double>> &sampleData,
                   const QDateTime &sampleDate);
};

#endif // CURVEDATABUFFER_H
