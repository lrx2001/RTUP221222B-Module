#include "curvedatabuffer.h"
#include <algorithm>

// 静态常量成员必须在某一 .cpp 中定义，否则链接时会出现 undefined reference
const int CurveDataBuffer::BufferSize;
const int CurveDataBuffer::CurveCount;

CurveDataBuffer::CurveDataBuffer()
{
    items.resize(CurveCount, std::vector<double>(BufferSize, 0.0));
    sampleTime.resize(BufferSize);
}

bool CurveDataBuffer::addCurve(int curveKey)
{
    if (curvesMap.count(curveKey))
        return true;
    if (static_cast<int>(curvesMap.size()) >= CurveCount)
        return false;
    int row = static_cast<int>(curvesMap.size());
    curvesMap[curveKey] = row;
    return true;
}

void CurveDataBuffer::removeCurve(int curveKey)
{
    curvesMap.erase(curveKey);
}

void CurveDataBuffer::resetCurveData()
{
    curvesMap.clear();
    for (auto &row : items)
        std::fill(row.begin(), row.end(), 0.0);
    count = 0;
    currentPos = 0;
    isOverlapped = false;
}

void CurveDataBuffer::addSample(const std::vector<std::pair<int, double>> &sampleData,
                                const QDateTime &sampleDate)
{
    int pos = count % BufferSize;
    for (const auto &kv : sampleData) {
        int curveKey = kv.first;
        double value = kv.second;
        auto it = curvesMap.find(curveKey);
        if (it != curvesMap.end()) {
            int row = it->second;
            items[row][pos] = value;
        }
    }
    sampleTime[pos] = sampleDate;
    count++;
    if (count > BufferSize)
        isOverlapped = true;
}
