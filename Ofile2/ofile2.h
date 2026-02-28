#ifndef OFILE2_H
#define OFILE2_H
#include "special_command.h"
#include "modify_configuration_parameters.h"
#include "compressor_status.h"
#include "single_valve_command.h"
#include "unlock.h"
#include "other_data_items.h"
#include "valvecommand.h"
#include "adjustcompressor.h"
#include "pushbutton_parameters.h"
#include "curvesdialog.h"
#include "playbacksettingsdialog.h"
#include "sqliterecorder.h"

#include <QMainWindow>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>
#include <QStringList>
#include <QString>
#include <QTime>
#include <QByteArray>
#include <QModbusClient>
#include <QtCharts>
#include <QMessageBox>
#include <QModbusDataUnit>
#include <QMap>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPropertyAnimation>
#include <QEvent>
#include <QScrollBar>
#include <QTimer>
#include <QSqlDatabase>
#include <QSqlQuery>



QT_BEGIN_NAMESPACE
namespace Ui { class Ofile2; }
QT_END_NAMESPACE

class Ofile2 : public QMainWindow
{
    Q_OBJECT

public:
    Ofile2(QWidget *parent = nullptr);
    ~Ofile2();
public:
    void initWindow();      //初始化桌面
    void showMessage(const char* str);

    int m_recvHex;  //接收
private slots:
    void onModbusStateChanged(QModbusDevice::State state); // 监听连接状态改变
    void updateConnectionTimer();                         // 每秒更新时间
    void on_checkBox_Reset_when_connecting_stateChanged(int arg1);              // 勾选清零处理


    void on_pushButton_Special_command_clicked();
    void on_pushButton_Modify_configuration_parameters_clicked();
    void on_pushButton_Compressor_Status_clicked();
    void on_pushButton_Single_valve_command_clicked();
    void on_pushButton_Unlock_clicked();
    void on_pushButton_Other_data_items_clicked();
    void on_button_Connect_clicked();
    void on_pushButton_Application_clicked();
    void on_ValveCommand_clicked();

    void on_AdjustCompressor_clicked();

    void on_pushButton_parameters_clicked();
    void on_pushButton_Curves_clicked();
    void on_actionShowCurves_triggered();
    void on_actionStartPlayback_triggered();
    void onCurveRecordTimer();
    void onPlaybackTimerTick();
    void on_slider_Playback_valueChanged(int value);


protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void SearchSerialPorts();

    // 初始化映射表
    void setupAddressMapping();

    // 用于初始化分块的函数
    void initializeModbusBlocks();

    // ReadRequest 现在是发送队列中的当前块
    void ReadRequest();

    // ReadSerialData 现在负责处理队列推进
    void ReadSerialData();

    // 更新故障显示栏
    void updateFaultDisplay();

    void setupCoilControls(); // 初始化配置

    void updateCoilUI();      // 轮询读取并同步 UI

    void setupControlSystem();// 统一初始化函数

    void setupHoldingControls();           // 初始化函数

    void setupLayouts(); // 新增：用于初始化布局和滚动逻辑

    QList<CurveOption> getCurveOptions(); // 供曲线对话框使用的数据项列表（地址+中文名+lineEdit）
    static bool tryParseDouble(const QString &text, double &outVal);
    void startSqliteRecording();
    void stopSqliteRecording();
    void stopPlayback();

private:
    Ui::Ofile2 *ui;

    QMap<int, QString> m_curveDisplayNames; // 曲线可选数据项：Modbus 地址 -> 中文名称（如 49->"高压1"）

    bool m_isChainPolling = false; // 是否处于链式轮询状态
    bool m_waitingForReply = false; // 【新增】防止并发请求的锁

    QTimer *m_durationTimer = nullptr;
    int m_totalSeconds = 0;



    QModbusClient *modbusDevice = nullptr;
    QTimer *pollTimer;
    uint16_t values[8];
    Single_valve_command *m_SingleValveDlg = nullptr; // 定义为成员变量
    ValveCommand *m_valveCommandDlg = nullptr;
    AdjustCompressor *m_adjustCompressorDlg = nullptr; // 定义为成员变量
    CurvesDialog *m_curvesDlg = nullptr;
    QTimer *m_curveRecordTimer = nullptr;
    SqliteRecorder *m_sqliteRecorder = nullptr;
    QString m_sqliteFilePath;

    // =========================
    // 回放（离线）相关成员变量
    // =========================
    // 设计目标：
    // - 录制阶段：不断向 SqliteRecorder 写入 samples 表。
    // - 回放阶段：从 .sqlite 读取 samples，灌入曲线，并在主界面显示“回放进度条 + 时间”。
    // - 既支持“像录像一样按时间间隔播放”，也支持“一次加载全部，手动拖动时间轴浏览”两种模式。
    QSqlDatabase m_playbackDb;
    QString m_playbackConnName;
    QString m_playbackFilePath;
    QTimer *m_playbackTimer = nullptr;
    QVector<qint64> m_playbackTsMs;  // 时间轴：每一帧对应的时间戳（ms），与曲线缓冲中的样本一一对应
    int m_playbackTsIndex = 0;       // 当前回放位置（索引到 m_playbackTsMs）
    bool m_playbackPaused = false;
    // =========================
    // 故障监控相关结构与成员变量
    // =========================
    // 设计目标：
    // - 支持“一个 16bit 寄存器里，每一位代表一个独立故障”的场景。
    // - 通过配置 FaultBitDefinition 列表，对某些地址的各个 bit 做锁存和消除逻辑，
    //   并把当前所有活动故障集中保存在 m_activeFaults 中，供右侧故障窗口统一显示。
        struct FaultBitDefinition {
            int bitPosition;    // 0-15：寄存器中的位索引
            QString message;    // 故障消息（UI 上展示的文本）
            int alertValue;     // 触发值 (通常是 1，少数协议也可能是 0)
        };

        // 专门用于多位故障寄存器 (例如地址 2002) 的配置：
        // 键：Modbus 地址；
        // 值：该地址下所有需要监控的 bit 及其含义列表。
        QMap<int, QList<FaultBitDefinition>> m_multiBitFaultMap;

        // 用于存储【纯故障监控】地址的列表（这些地址只需要读取，但不需要更新到任何 QLineEdit）。
        // 典型用途：某些寄存器只用来挂故障，并不在主数据页显示原始数值。
        QMap<int, QList<FaultBitDefinition>> m_addressToMonitorMap; // <-- 新增！

        // 存储当前所有活动故障的列表。
        // 键格式："地址_位索引"（例如："2002_B5"），值为对应的故障消息。
        // 这样可以方便地在故障消除时根据键快速删除，也方便在 UI 上唯一标识一条故障。
        QMap<QString, QString> m_activeFaults;

    // =========================
    // 定义一个结构体来封装“地址 -> 文本框”的数值转换规则
    // =========================
    // 设计目标：
    // - 一张表（m_addressToRuleMap）即可描述：某个 Modbus 地址如何映射到一个或多个 QLineEdit。
    // - 通过 ruleType、valueMap、bitPosition 等字段，支持多种显示方式：
    //   1. 直接整数显示（支持负数）
    //   2. 除以 10 显示小数（温度 / 压力常用）
    //   3. 提取某一个 bit 显示 0/1
    //   4. 监控某个 bit 触发告警（配合 FaultBitDefinition）
    //   5. 数值映射到文字（运行模式、风机状态等）
    //   6. 单 bit 映射到两段文字（如“开/关”“有/无”等）
    struct ConversionRule {

        QLineEdit *lineEdit = nullptr; // 对应的输入框
        int ruleType = 0;        // 转换规则类型
        int bitPosition = -1; // 要提取的位索引 (0-15)
        // 新增：用于位转文本的灵活显示
        QString textFor0; // 位为 0 时显示的文本
        QString textFor1; // 位为 1 时显示的文本
        // 告警监控：只有 Rule_MonitorBit 规则才使用这些字段
        QString alertMessage; // 告警时显示的文本（例如：“压缩机通讯故障”）
        int alertValue ;  // 触发告警的值 (例如：如果监控位为 1，则触发)
        // 用于值到文本映射的规则
        QMap<quint16, QString> valueMap;
        // 显示格式字段
        QString prefix;       // 前缀 (例如: "-")
        QString suffix;       // 后缀 (例如: "°C", "HZ")

        // 规则类型枚举 (可根据实际情况调整)
        enum {
            Rule_None = 0,               // 默认：直接显示整数
            Rule_DivideBy10_1DP = 1,     // 除以 10，保留一位小数 (例如：310 -> 31.0)
            Rule_ExtractBit = 2,       // 通用位提取规则
            Rule_MonitorBit = 3,         // 监控特定位并触发告警/特殊日志
            Rule_ValueMap_Text = 4,   // 值到文本描述映射
            Rule_BitToText = 5,  // 新增：位转文本规则
        };

        // 确保有一个空的默认构造函数，以防 QMap 内部需要它
        ConversionRule() : lineEdit(nullptr), ruleType(Rule_None), bitPosition(-1), alertMessage(""), alertValue(-1) {}


        // 1. Rule_None / Rule_DivideBy10_1DP / 简单规则 (2个参数: 控件, 规则类型)
        // 自动初始化 bitPosition, alertMessage, alertValue 为默认值
        ConversionRule(QLineEdit *line, int type)
            : lineEdit(line), ruleType(type), bitPosition(-1), alertMessage(""), alertValue(-1)
        {}

        // 2. Rule_ExtractBit (3个参数: 控件, 规则类型, 位索引)
        // 自动初始化 alertMessage, alertValue 为默认值
        ConversionRule(QLineEdit *line, int type, int bitPos)
            : lineEdit(line), ruleType(type), bitPosition(bitPos), alertMessage(""), alertValue(-1)
        {}

        // 3. Rule_MonitorBit (5个参数: 控件, 规则类型, 位索引, 告警消息, 告警值)
        // 使用所有参数
        ConversionRule(QLineEdit *line, int type, int bitPos, const QString &msg, int val)
            : lineEdit(line), ruleType(type), bitPosition(bitPos), alertMessage(msg), alertValue(val)
        {}

        // 【构造函数】Rule_ValueMap_Text 专用构造函数 (3个参数: 控件, 规则类型, 映射表)
        ConversionRule(QLineEdit *line, int type, const QMap<quint16, QString> &map)
            : lineEdit(line), ruleType(type), bitPosition(-1), alertMessage(""), alertValue(-1), valueMap(map)
        {}

        // 5. 【构造函数】Rule_DivideBy10_1DP + 前后缀 (4个参数: 控件, 规则类型, 后缀)
        ConversionRule(QLineEdit *line, int type, const QString &suf)
            : lineEdit(line), ruleType(type), bitPosition(-1), alertMessage(""), alertValue(-1), valueMap(), suffix(suf)
        {}

        // 5. 【构造函数】Rule_DivideBy10_1DP + 前后缀 (4个参数: 控件, 规则类型, 前缀, 后缀)
        ConversionRule(QLineEdit *line, int type, const QString &pre, const QString &suf)
            : lineEdit(line), ruleType(type), bitPosition(-1), alertMessage(""), alertValue(-1), valueMap(), prefix(pre), suffix(suf)
        {}


        // 修改构造函数以支持灵活文本
        ConversionRule(QLineEdit *line, int type, int bitPos, const QString &t0, const QString &t1)
            : lineEdit(line), ruleType(type), bitPosition(bitPos), textFor0(t0), textFor1(t1) {}

    };

    //Modbus 地址到转换规则的映射表
    QMultiMap<int, ConversionRule> m_addressToRuleMap;

    struct ModbusBlock {
        int startAddress;
        quint16 numberOfEntries;
    };
    // 用于索引当前正在处理的块
    int m_currentBlockIndex = 0;
    // 请求队列，存储需要发送的所有块
    QList<ModbusBlock> m_requestQueue;

    // 统一控制配置结构体，支持不同寄存器类型和自定义文本
    struct ControlConfig {
        QComboBox *comboBox;
        QModbusDataUnit::RegisterType type; // Coils 或 HoldingRegisters
        int address;
        QString textFor0; // 对应值 0 的文本
        QString textFor1; // 对应值 1 的文本
    };

    QList<ControlConfig> m_controlConfigs;

    // 专门针对 HoldingRegisters 的配置结构
    struct HoldingConfig {
        QComboBox *comboBox;
        int address;
        QStringList options; // 支持大量选项
        int offset;           // 数值偏移量（例如：索引0对应值1，则偏移量为1）
    };

    QList<HoldingConfig> m_holdingConfigs; // 存储所有的保持寄存器控制项

};
#endif // OFILE2_H
