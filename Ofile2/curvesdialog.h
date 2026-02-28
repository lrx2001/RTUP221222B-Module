#ifndef CURVESDIALOG_H
#define CURVESDIALOG_H

#include <QDialog>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>
#include <QList>
#include <QListWidget>
#include <QTableWidget>
#include <QMap>
#include <QShowEvent>
#include <QHideEvent>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QScrollArea>
#include <QFrame>
#include "curveswidget.h"

class QModelIndex;
class QEvent;
class QObject;
class QPoint;

struct CurveOption
{
    int curveKey;            // 曲线唯一标识，与 CANAC 一致可为 (Address<<16)+Index，单机时=Modbus 地址
    QString displayName;     // 中文名称，如「高压1」
    QString unit;            // 单位，如「℃」「Bar」
    QLineEdit *lineEdit = nullptr;
    // 可选：用于状态类通道（0/1/2...）的“数值->文字”映射，供曲线图例在历史位置反查显示
    QMap<quint16, QString> valueTextMap;
};

class CurvesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CurvesDialog(QWidget *parent = nullptr);

    void setCurveOptions(const QList<CurveOption> &options);
    void addSample(const std::vector<std::pair<int, double>> &sampleData,
                   const QDateTime &sampleDate);
    void recordCurrentValuesFromOptions();
    CurvesWidget *curvesWidget() { return m_curvesWidget; }

private slots:
    void onSliderValueChanged(int value);
    void onCurrentValueChanged(int pos);
    void onConfigClicked();
    void onSampleTimer();
    void refreshLegendAndStatus();
    void onLegendContextMenu(QPoint pos);
    void onLegendCellDoubleClicked(int row, int column);
    void removeCurveAtLegendRow(int row);
    void setCurveColorAtLegendRow(int row);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    CurvesWidget *m_curvesWidget;
    QSlider *m_slider;
    QPushButton *m_configBtn;
    QLabel *m_timeLabel;
    QLabel *m_valueLabel;
    QLabel *m_statusTimeLabel;
    QLabel *m_statusDurationLabel;
    QWidget *m_leftPanel = nullptr;
    QTableWidget *m_legendTable = nullptr;
    static const QColor s_nameBackgroundColor;
    static const QColor s_nameHiddenBackgroundColor;

    QList<CurveOption> m_curveOptions;
    QMap<int, QLineEdit*> m_selectedLineEdits;
    QList<int> m_selectedCurveKeyOrder;
    QMap<int, QString> m_curveKeyToDisplayName;
    QMap<int, QString> m_curveKeyToUnit;
    QMap<int, QMap<quint16, QString>> m_curveKeyToValueTextMap;
    QTimer *m_sampleTimer = nullptr;
    QTimer *m_legendRefreshTimer = nullptr;
    bool m_legendDragging = false;
    int m_legendDragSourceRow = -1;
    QPoint m_legendDragStartPos;
    bool m_isFullScreen = false;
    QRect m_normalGeometry;
    static const QList<QColor> s_curveColors;
    static double valueFromLineEdit(QLineEdit *le);
};

#endif // CURVESDIALOG_H
