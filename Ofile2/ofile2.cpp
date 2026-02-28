#include "ofile2.h"
#include "ui_ofile2.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QSet>
#include <QListView>
#include <QWidget>
#include <QDebug>
#include <QModbusRtuSerialMaster>
#include "modify_configuration_parameters.h"
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QGraphicsDropShadowEffect>
#include <QCloseEvent>
#include <QDir>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSqlError>
#include <QFileInfo>




Ofile2::Ofile2(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Ofile2)
{
    ui->setupUi(this);
    // 将"开始回放"action直接添加到菜单栏，点击即可触发（不需要展开子菜单）
    ui->menuBar->addAction(ui->actionShowCurves);
    ui->menuBar->addAction(ui->actionStartPlayback);
    setWindowTitle(tr("水机监控"));
    // 1. 调用封装好的布局函数
    setupLayouts();
    modbusDevice = new QModbusRtuSerialMaster(this); //串行总线
    pollTimer = new QTimer;
    initWindow();
    setupAddressMapping();
    setupControlSystem();
    setupHoldingControls();

    // 在构造函数内部添加
    m_durationTimer = new QTimer(this);
    connect(m_durationTimer, &QTimer::timeout, this, &Ofile2::updateConnectionTimer);
    m_curveRecordTimer = new QTimer(this);
    connect(m_curveRecordTimer, &QTimer::timeout, this, &Ofile2::onCurveRecordTimer);
    // 关键：不改动连接按钮，直接监听设备状态信号
    connect(modbusDevice, &QModbusClient::stateChanged, this, &Ofile2::onModbusStateChanged);

    m_playbackTimer = new QTimer(this);
    connect(m_playbackTimer, &QTimer::timeout, this, &Ofile2::onPlaybackTimerTick);

    // 回放控制条初始化（位于数据页面“测试拨码按钮”下方）
    if (ui->label_PlaybackPrefix) {
        ui->label_PlaybackPrefix->setText(tr("回放："));
        ui->label_PlaybackPrefix->hide();
    }
    if (ui->slider_Playback) {
        ui->slider_Playback->setMinimum(0);
        ui->slider_Playback->setMaximum(0);
        ui->slider_Playback->setValue(0);
        ui->slider_Playback->setEnabled(false);
        ui->slider_Playback->hide();
    }
    if (ui->label_PlaybackTotal) {
        ui->label_PlaybackTotal->setText(tr("总计：0 秒"));
        ui->label_PlaybackTotal->hide();
    }
    if (ui->label_PlaybackCurrent) {
        ui->label_PlaybackCurrent->setText(tr("当前位置：-"));
        ui->label_PlaybackCurrent->hide();
    }
}

Ofile2::~Ofile2()
{
    stopPlayback();
    stopSqliteRecording();
    delete ui;
}

void Ofile2::closeEvent(QCloseEvent *event)
{
    stopPlayback();
    stopSqliteRecording();
    QMainWindow::closeEvent(event);
}

bool Ofile2::tryParseDouble(const QString &text, double &outVal)
{
    QString s = text.trimmed();
    if (s.isEmpty()) return false;
    bool ok = false;
    double val = s.toDouble(&ok);
    if (ok) { outVal = val; return true; }
    // 提取第一个数字（支持单位后缀，如 64.2℃）
    QRegularExpression re(QStringLiteral("([-+]?[0-9]*\\.?[0-9]+(?:[eE][-+]?[0-9]+)?)"));
    QRegularExpressionMatch m = re.match(s);
    if (m.hasMatch()) {
        val = m.captured(1).toDouble(&ok);
        if (ok) { outVal = val; return true; }
    }
    return false;
}

void Ofile2::startSqliteRecording()
{
    if (!modbusDevice || modbusDevice->state() != QModbusDevice::ConnectedState)
        return;

    if (!m_sqliteRecorder)
        m_sqliteRecorder = new SqliteRecorder(this);

    if (m_sqliteRecorder->isActive())
        return;

    // 按日期创建文件夹：程序目录/Data/YYYY-M-D/
    QDateTime now = QDateTime::currentDateTime();
    const QString dateFolder = QStringLiteral("%1-%2-%3")
        .arg(now.date().year())
        .arg(now.date().month())
        .arg(now.date().day());
    QDir baseDir(QCoreApplication::applicationDirPath());
    const QString dataBasePath = baseDir.filePath(QStringLiteral("Data"));
    QDir dataBaseDir(dataBasePath);
    const QString dataDir = dataBaseDir.filePath(dateFolder);
    QDir().mkpath(dataDir);
    
    // 文件名仍包含时分秒，避免同一天多次连接时覆盖
    const QString fileName = QStringLiteral("record_%1.sqlite")
        .arg(now.toString(QStringLiteral("yyyyMMdd_HHmmss")));
    m_sqliteFilePath = QDir(dataDir).filePath(fileName);

    m_sqliteRecorder->start(m_sqliteFilePath, getCurveOptions());
}

void Ofile2::stopSqliteRecording()
{
    if (m_sqliteRecorder) {
        m_sqliteRecorder->stop();
    }
}

void Ofile2::stopPlayback()
{
    if (m_playbackTimer && m_playbackTimer->isActive())
        m_playbackTimer->stop();
    m_playbackTsMs.clear();
    m_playbackTsIndex = 0;
    m_playbackPaused = false;

    // 重置并隐藏回放控制条
    if (ui->label_PlaybackPrefix) {
        ui->label_PlaybackPrefix->hide();
    }
    if (ui->slider_Playback) {
        ui->slider_Playback->setEnabled(false);
        ui->slider_Playback->setMinimum(0);
        ui->slider_Playback->setMaximum(0);
        ui->slider_Playback->setValue(0);
        ui->slider_Playback->hide();
    }
    if (ui->label_PlaybackTotal) {
        ui->label_PlaybackTotal->setText(tr("总计：0 秒"));
        ui->label_PlaybackTotal->hide();
    }
    if (ui->label_PlaybackCurrent) {
        ui->label_PlaybackCurrent->setText(tr("当前位置：-"));
        ui->label_PlaybackCurrent->hide();
    }

    if (m_playbackDb.isValid()) {
        if (m_playbackDb.isOpen())
            m_playbackDb.close();
    }
    if (!m_playbackConnName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_playbackConnName);
    }
    m_playbackConnName.clear();
    m_playbackFilePath.clear();
    m_playbackDb = QSqlDatabase();
}


void Ofile2::initWindow()
{
    //填充串口号组合框
    SearchSerialPorts();

    QStringList list;       //字符串列表
    list.clear();           //列表清空设置
    list << "2400" << "4800" << "9600" << "14400" << \
         "19200" << "38400" << "43000" << "57600" << "76800" << \
         "115200" << "230400" << "256000" << "460800" << "921600";
    ui->comboBox_Baud_rate->addItems(list);     //波特率列表
    ui->comboBox_Baud_rate->setCurrentText(tr("9600"));   //设置默认状态显示

    ui->plainTextEdit->setPlaceholderText("在此处接收数据");
}

void Ofile2::SearchSerialPorts()
{
    ui->comboBox_Serial_Port_Selection->clear();

    foreach(const QSerialPortInfo &info,QSerialPortInfo::availablePorts())
    {
        ui->comboBox_Serial_Port_Selection->addItem(info.portName());
    }
}

//读数据请求
void Ofile2::ReadRequest()
{
    if (!modbusDevice|| modbusDevice->state() != QModbusDevice::ConnectedState)
    {
//        QMessageBox::information(NULL,  "Title",  "尚未连接从站设备");
        return;
    }
    // 【优化点】获取当前要请求的 Modbus 块信息
    // 使用 m_currentBlockIndex 索引到队列中的 ModbusBlock
    const ModbusBlock &currentBlock = m_requestQueue.at(m_currentBlockIndex);
    QModbusDataUnit readUnit(QModbusDataUnit::InputRegisters, currentBlock.startAddress, currentBlock.numberOfEntries);
    //    QModbusDataUnit::RegisterType type = QModbusDataUnit::InputRegisters;
    //    QModbusDataUnit readUnit=QModbusDataUnit(type, startAddress,numberOfEntries);
    statusBar()->clearMessage();

    if (auto *reply = modbusDevice->sendReadRequest(readUnit,1))
    {
        if (!reply->isFinished())
            connect(reply, &QModbusReply::finished, this, &Ofile2::ReadSerialData);
        else
            delete reply; // broadcast replies return immediately
    }
    else
    {
        // 即使发送失败，也要尝试推进到下一个请求，避免卡死
        statusBar()->showMessage(tr("Read error: ") + modbusDevice->errorString(), 5000);
        // 失败后推进队列并重启定时器（如果需要连续读取）
        m_currentBlockIndex = (m_currentBlockIndex + 1) % m_requestQueue.size();
    }
}

//从串口接收数据
void Ofile2::ReadSerialData()
{
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply)
        return;

    if (reply->error() == QModbusDevice::NoError)
    {
        const QModbusDataUnit unit = reply->result();

        for (int i = 0, total = int(unit.valueCount()); i < total; ++i)
        {
            const int currentAddress = unit.startAddress() + i;
            const quint16 currentValue = unit.value(i);

            QString displayValue = QString::number(currentValue); // 默认显示原始值
            QString logEntry;
            QString logColor = "black";

            // 默认日志格式：在所有规则检查前初始化
            logEntry = tr("Address: %1, Value: %2 (Hex: %3)")
                    .arg(currentAddress)
                    .arg(currentValue)
                    .arg(QString::number(currentValue, 16));


            // =======================================================
            // 阶段 A: 【核心】统一故障检查、锁存与清除
            // =======================================================

            QList<FaultBitDefinition> currentFaultList; // 用于存储当前要检查的故障列表

            // 1. 检查纯故障监控列表 (最高优先级)
            if (m_addressToMonitorMap.contains(currentAddress))
            {
                currentFaultList = m_addressToMonitorMap.value(currentAddress);
            }
            // 2. 检查 UI 绑定列表 (兼容 m_multiBitFaultMap，如果仍在使用它)
            else if (m_multiBitFaultMap.contains(currentAddress))
            {
                currentFaultList = m_multiBitFaultMap.value(currentAddress);
            }

            // 检查是否有要监控的故障位：
            // currentFaultList 来源：
            //  - m_multiBitFaultMap：寄存器里的各个 bit 代表不同故障
            //  - m_addressToMonitorMap：仅用于监控的地址（不在 UI 上显示原始数值）
            if (!currentFaultList.isEmpty())
            {
                // 遍历该地址定义的所有故障位，独立检查和锁存
                for (const auto &faultDef : currentFaultList)
                {
                    int bitValue = (currentValue >> faultDef.bitPosition) & 0x01;
                    const QString &faultMessage = faultDef.message;

                    // 构造唯一的故障键：地址_位索引 (例如：2004_B2)
                    QString faultMapKey = QString("%1_B%2").arg(currentAddress).arg(faultDef.bitPosition);

                    // 1. 故障发生 (锁存)：
                    //    当检测到某个故障位从“无故障”变为“故障触发值”时，
                    //    将其加入 m_activeFaults，并在日志中打印一条红色记录。
                    if (bitValue == faultDef.alertValue)
                    {
                        if (!m_activeFaults.contains(faultMapKey)) {
                            // 首次触发，添加至活动列表
                            m_activeFaults.insert(faultMapKey, faultMessage);

                            // 记录到主日志
                            logColor = "red";
                            logEntry = tr("【告警触发】Address: %1, Bit %2 = %3, %4").arg(currentAddress).arg(faultDef.bitPosition).arg(bitValue).arg(faultMessage);

                        }
                    }
                    // 2. 故障消除：
                    //    当该位不再等于 alertValue 时，如果之前处于活动列表中，
                    //    将其从 m_activeFaults 删除，并打印一条绿色“清除”日志。
                    else
                    {
                        if (m_activeFaults.contains(faultMapKey)) {
                            // 故障消除，从活动列表中移除
                            m_activeFaults.remove(faultMapKey);

                            // 记录清除信息
                            logColor = "green";
                            logEntry = tr("【清除】Address: %1, Bit %2, %3 消除").arg(currentAddress).arg(faultDef.bitPosition).arg(faultMessage);
                        }
                    }
                }
            }

            // =======================================================
            // 阶段 B: UI 更新和数值转换 (支持一地址多控件映射)
            // =======================================================
            QList<ConversionRule> rules = m_addressToRuleMap.values(currentAddress);
            for (const ConversionRule &rule : rules) {
                if (!rule.lineEdit) continue;

                QString displayValue;

                // 每次更新前，先清空曲线用的原始数值属性和映射，防止残留
                rule.lineEdit->setProperty("curveNumericValue", QVariant());
                rule.lineEdit->setProperty("curveValueMap", QVariant());

                // 将无符号原始值转换为有符号 16 位整数，以支持负数显示
                qint16 signedValue = static_cast<qint16>(currentValue);

                // --- 执行数据转换 ---
                switch (rule.ruleType) {
                case ConversionRule::Rule_BitToText: // 新增逻辑
                {
                    if (rule.bitPosition >= 0 && rule.bitPosition <= 15) {
                        int bitValue = (currentValue >> rule.bitPosition) & 0x01;
                        displayValue = (bitValue == 1) ? rule.textFor1 : rule.textFor0;
                        // 对于位文本，曲线上仍使用 0/1 数值
                        rule.lineEdit->setProperty("curveNumericValue", bitValue);

                        // 为图例准备“数值->文字”映射
                        QVariantMap vm;
                        vm.insert(QString::number(0), rule.textFor0);
                        vm.insert(QString::number(1), rule.textFor1);
                        rule.lineEdit->setProperty("curveValueMap", vm);
                    }
                    break;
                }
                case ConversionRule::Rule_DivideBy10_1DP:
                {
                    // 支持负数小数显示
                    displayValue = QString::number(static_cast<double>(signedValue) / 10.0, 'f', 1);
                    break;
                }
                case ConversionRule::Rule_ExtractBit: // <-- 补充此逻辑
                {
                    if (rule.bitPosition >= 0 && rule.bitPosition <= 15) {
                        int bitValue = (currentValue >> rule.bitPosition) & 0x01;
                        displayValue = QString::number(bitValue);
                    }
                    break;
                }
                case ConversionRule::Rule_ValueMap_Text:
                {
                    if (rule.valueMap.contains(currentValue)) {
                        displayValue = rule.valueMap.value(currentValue);
                    } else {
                        displayValue = tr("未知状态: %1").arg(currentValue);
                    }
                    // 值到文本映射：曲线使用原始寄存器值 0/1/2...
                    rule.lineEdit->setProperty("curveNumericValue", static_cast<int>(currentValue));

                    // 为图例准备“数值->文字”映射
                    if (!rule.valueMap.isEmpty()) {
                        QVariantMap vm;
                        for (auto itMap = rule.valueMap.constBegin(); itMap != rule.valueMap.constEnd(); ++itMap) {
                            vm.insert(QString::number(itMap.key()), itMap.value());
                        }
                        rule.lineEdit->setProperty("curveValueMap", vm);
                    }
                    break;
                }
                case ConversionRule::Rule_None:
                default:
                {
                    // 默认整数支持负数显示
                    displayValue = QString::number(signedValue);
                    break;
                }
                }
                // 应用前后缀
                if (rule.ruleType == ConversionRule::Rule_DivideBy10_1DP || rule.ruleType == ConversionRule::Rule_None)
                {
                    if (!rule.prefix.isEmpty()) {
                        displayValue.prepend(rule.prefix);
                    }
                    if (!rule.suffix.isEmpty()) {
                        displayValue.append(rule.suffix);
                    }
                }

                // --- 更新 UI ---
                rule.lineEdit->setText(displayValue);
            }

            // =======================================================
            // 阶段 D: 统一日志输出 (确保只输出一次)
            // =======================================================
            const QString formattedEntry = tr("<font color=\"%1\">%2</font>").arg(logColor).arg(logEntry);
            //ui->textBrowser->append(formattedEntry);
        }

        // 循环结束后，刷新故障显示栏
        updateFaultDisplay();

    }

    // 【优化点】推进到下一个请求块的索引
    if (!m_requestQueue.isEmpty())
    {
        m_currentBlockIndex = (m_currentBlockIndex + 1) % m_requestQueue.size();
    }

    reply->deleteLater();
}

void Ofile2::on_pushButton_Special_command_clicked()
{
    // 1. 弹出前停止主界面的自动轮询
    if (pollTimer->isActive()) {
        pollTimer->stop();
    }
    // 将主窗口的 modbusDevice 传入
    Special_command Special(modbusDevice, this);
    Special.setWindowTitle(tr("特殊命令"));
    Special.exec(); // 模态显示

    // 3. 窗口关闭后，如果还是连接状态，恢复轮询
    if (modbusDevice->state() == QModbusDevice::ConnectedState) {
        pollTimer->start(100); // 假设您的轮询周期是 1000ms
    }
}

void Ofile2::on_pushButton_Modify_configuration_parameters_clicked()
{
    // 1. 停止主界面的轮询，防止总线竞争
    if (m_isChainPolling) {
        m_isChainPolling = false;
    }

    // 2. 创建并显示模态对话框
    Modify_configuration_parameters configDlg(modbusDevice, this);
    configDlg.setWindowTitle(tr("配置参数设置"));

    // exec() 会阻塞主循环，直到窗口关闭
    configDlg.exec();

    // 3. 窗口关闭后，恢复主界面轮询
    m_isChainPolling = true;
    m_waitingForReply = false;
    ReadRequest();
}

void Ofile2::on_pushButton_Compressor_Status_clicked()
{
    Compressor_Status configWindow(this);
    configWindow.exec();
}

void Ofile2::on_pushButton_Single_valve_command_clicked()
{
    // 如果窗口还没创建，则创建它
    if (!m_SingleValveDlg) {
        m_SingleValveDlg = new Single_valve_command(modbusDevice, this);
    }

    // 每次弹出前，手动触发一次读取请求
    // 这样如果连接着，它会更新；如果没连接，它会保留上次的文本

    m_SingleValveDlg->readAllValves();
    m_SingleValveDlg->exec(); // 模态显示
}

void Ofile2::on_pushButton_Unlock_clicked()
{
    Unlock configWindow(this);
    configWindow.exec();
}

void Ofile2::on_pushButton_Other_data_items_clicked()
{
    Other_data_items configWindow (this);
    configWindow.exec();
}



void Ofile2::setupAddressMapping()
{
    // 清空现有映射
    m_addressToRuleMap.clear();
    m_addressToMonitorMap.clear(); // <-- 新增清除

    // =======================================================
    // I. UI/数值显示规则 (m_addressToRuleMap)
    // =======================================================
    // 格式：m_addressToRuleMap.insert(地址, {对应的QLineEdit指针, 规则类型});

    // 示例 1: 地址 95 需要除以 10，保留一位小数 (两参数构造函数)


    m_addressToRuleMap.insert(1, {ui->lineEdit_Excessive_discharge, ConversionRule::Rule_DivideBy10_1DP,"℃"});//总出水温度
    m_addressToRuleMap.insert(32, {ui->lineEdit_Water_ingress, ConversionRule::Rule_DivideBy10_1DP,"℃"});//进水温度
    m_addressToRuleMap.insert(33, {ui->lineEdit_Water_outlet, ConversionRule::Rule_DivideBy10_1DP,"℃"});//出水温度
    m_addressToRuleMap.insert(34, {ui->lineEdit_ambient_temperature, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(35, {ui->lineEdit_Inhale_1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(36, {ui->lineEdit_Inhale_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});

    //从机吸气
    m_addressToRuleMap.insert(135, {ui->lineEdit_Accessory_inhalation1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(136, {ui->lineEdit_Accessory_inhalation2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(137, {ui->lineEdit_Accessory_inhalation3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(138, {ui->lineEdit_Accessory_inhalation4, ConversionRule::Rule_DivideBy10_1DP});
    //系统1无霜环翅温差
    m_addressToRuleMap.insert(3003, {ui->lineEdit_Frost_free_ring_wing_temperature_difference1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(3004, {ui->lineEdit_Frost_free_ring_wing_temperature_difference2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(3005, {ui->lineEdit_Frost_free_ring_wing_temperature_difference3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(3006, {ui->lineEdit_Frost_free_ring_wing_temperature_difference4, ConversionRule::Rule_DivideBy10_1DP});
    //从系统1无霜环翅温差
    m_addressToRuleMap.insert(4003, {ui->lineEdit_Temperature_difference_of_frost_free_annular_fins1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(4004, {ui->lineEdit_Temperature_difference_of_frost_free_annular_fins2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(4005, {ui->lineEdit_Temperature_difference_of_frost_free_annular_fins3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(4006, {ui->lineEdit_Temperature_difference_of_frost_free_annular_fins4, ConversionRule::Rule_DivideBy10_1DP});

    //程序版本号
    m_addressToRuleMap.insert(95, {ui->lineEdit_Software_Version, ConversionRule::Rule_DivideBy10_1DP});

    // 需求：地址 3009 的 bit5 显示在 Ui地址，内容为“开/关”→铺热
    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_Auxiliary_heat,
                                                   ConversionRule::Rule_BitToText,1, "关", "开"));
    // 需求：地址 3009 的 bit5 显示在 Ui地址，内容为“开/关”→铺热
    m_addressToRuleMap.insert(4009, ConversionRule(ui->lineEdit_from_Auxiliary_heating,
                                                   ConversionRule::Rule_BitToText,1, "关", "开"));
    // 需求：地址 3009 的 bit5 显示在 Ui地址，内容为“开/关”→铺热
    m_addressToRuleMap.insert(4009, ConversionRule(ui->lineEdit_Electric_heating,
                                                   ConversionRule::Rule_BitToText,1, "关", "开"));
    // 水泵1
    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_Water_Pump_1,
                                                   ConversionRule::Rule_BitToText,0, "停止", "运行"));
    // 水泵2
    m_addressToRuleMap.insert(4009, ConversionRule(ui->lineEdit_Water_Pump_2,
                                                   ConversionRule::Rule_BitToText,0, "停止", "运行"));
    m_addressToRuleMap.insert(5009, ConversionRule(ui->lineEdit_Water_Pump_3,
                                                   ConversionRule::Rule_BitToText,0, "停止", "运行"));
    // 四通阀
    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_4WV1,
                                                   ConversionRule::Rule_BitToText,5, "关", "开"));
    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_4WV2,
                                                   ConversionRule::Rule_BitToText,10, "关", "开"));
    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_4WV3,
                                                   ConversionRule:: Rule_BitToText,15, "关", "开"));
    m_addressToRuleMap.insert(3010, ConversionRule(ui->lineEdit_4WV4,
                                                   ConversionRule::Rule_BitToText,4, "关", "开"));
    // 从四通阀
    m_addressToRuleMap.insert(4009, ConversionRule(ui->lineEdit_from_4WV1,
                                                   ConversionRule::Rule_BitToText,5, "关", "开"));
    m_addressToRuleMap.insert(4009, ConversionRule(ui->lineEdit_from_4WV2,
                                                   ConversionRule::Rule_BitToText,10, "关", "开"));
    m_addressToRuleMap.insert(4009, ConversionRule(ui->lineEdit_from_4WV3,
                                                   ConversionRule:: Rule_BitToText,15, "关", "开"));
    m_addressToRuleMap.insert(4010, ConversionRule(ui->lineEdit_from_4WV4,
                                                   ConversionRule::Rule_BitToText,4, "关", "开"));
    // 低压开关1
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_Low_voltage_switch1,
                                                   ConversionRule::Rule_BitToText,0, "关", "开"));
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_Low_voltage_switch2,
                                                   ConversionRule::Rule_BitToText,1, "关", "开"));
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_Low_voltage_switch3,
                                                   ConversionRule:: Rule_BitToText,2, "关", "开"));
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_Low_voltage_switch4,
                                                   ConversionRule::Rule_BitToText,3, "关", "开"));
    // 从低压开关1
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Auxiliary_Low_Pressure_Switch1,
                                                   ConversionRule::Rule_BitToText,0, "关", "开"));
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Auxiliary_Low_Pressure_Switch2,
                                                   ConversionRule::Rule_BitToText,1, "关", "开"));
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Auxiliary_Low_Pressure_Switch3,
                                                   ConversionRule:: Rule_BitToText,2, "关", "开"));
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Auxiliary_Low_Pressure_Switch4,
                                                   ConversionRule::Rule_BitToText,3, "关", "开"));

    // 高压开关1
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_High_voltage_switch1,
                                                   ConversionRule::Rule_BitToText,7, "关", "开"));
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_High_voltage_switch2,
                                                   ConversionRule::Rule_BitToText,8, "关", "开"));
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_High_voltage_switch3,
                                                   ConversionRule:: Rule_BitToText,9, "关", "开"));
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_High_voltage_switch4,
                                                   ConversionRule::Rule_BitToText,10, "关", "开"));
    // 从高压开关1
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Medium_High_Voltage_Switch1,
                                                   ConversionRule::Rule_BitToText,7, "关", "开"));
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Medium_High_Voltage_Switch2,
                                                   ConversionRule::Rule_BitToText,8, "关", "开"));
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Medium_High_Voltage_Switch3,
                                                   ConversionRule:: Rule_BitToText,9, "关", "开"));
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Medium_High_Voltage_Switch4,
                                                   ConversionRule::Rule_BitToText,10, "关", "开"));
    // 水流开关
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_Flow_switch,
                                                   ConversionRule::Rule_BitToText,6, "关", "开"));
    // 从水流开关
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Auxiliary_Water_Flow_Switch,
                                                   ConversionRule::Rule_BitToText,6, "关", "开"));
    // 联动开关
    m_addressToRuleMap.insert(3011, ConversionRule(ui->lineEdit_Interlocking_switch,
                                                   ConversionRule::Rule_BitToText,4, "关", "开"));
    // 从联动开关
    m_addressToRuleMap.insert(4011, ConversionRule(ui->lineEdit_Auxiliary_switch,
                                                   ConversionRule::Rule_BitToText,4, "关", "开"));

    m_addressToRuleMap.insert(77, {ui->lineEdit_Fan_Setting_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(78, {ui->lineEdit_Fan_Setting_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(79, {ui->lineEdit_Fan_Setting_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(80, {ui->lineEdit_Fan_Setting_4, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(81, {ui->lineEdit_Fan_Actual_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(82, {ui->lineEdit_Fan_Actual_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(83, {ui->lineEdit_Fan_Actual_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(84, {ui->lineEdit_Fan_Actual_4, ConversionRule::Rule_None});


    //低压
    m_addressToRuleMap.insert(45, {ui->lineEdit_System_1_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(46, {ui->lineEdit_System_2_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(47, {ui->lineEdit_Deputy_System_3_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(48, {ui->lineEdit_Deputy_System_4_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    //排气温度
    // Address 50 (外环 3) 使用 Rule_DivideBy10_1DP，需要 '°C' 后缀 (四参数构造函数)
    m_addressToRuleMap.insert(28, {ui->lineEdit_Exhaust_1,ConversionRule::Rule_DivideBy10_1DP,"℃"});
//    m_addressToRuleMap.insert(28, {ui->lineEdit_Exhaust_1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(29, {ui->lineEdit_Exhaust_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(30, {ui->lineEdit_Deputy_Exhaust_3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(31, {ui->lineEdit_Deputy_Exhaust_4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //回气温度
    m_addressToRuleMap.insert(35, {ui->lineEdit_Inhale_1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(36, {ui->lineEdit_Inhale_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(37, {ui->lineEdit_Deputy_Inhale_3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(38, {ui->lineEdit_Deputy_Inhale_4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //盘管温度
    m_addressToRuleMap.insert(39, {ui->lineEdit_Pipe_Coil_1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(40, {ui->lineEdit_Pipe_Coil_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(41, {ui->lineEdit_Deputy_Pipe_Coil_3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(42, {ui->lineEdit_Deputy_Pipe_Coil_4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //排气目标过热度1
    m_addressToRuleMap.insert(43, {ui->lineEdit_Exhaust_Target_Overheat1, ConversionRule::Rule_DivideBy10_1DP});
    //排气实际过热度1
    m_addressToRuleMap.insert(61, {ui->lineEdit_Exhaust_Overtemperature1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(62, {ui->lineEdit_Exhaust_Overtemperature2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(63, {ui->lineEdit_Exhaust_Overtemperature3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(64, {ui->lineEdit_Exhaust_Overtemperature4, ConversionRule::Rule_DivideBy10_1DP});
    //从排气目标过热度1
    m_addressToRuleMap.insert(143, {ui->lineEdit_From_exhaust_target_overheating_level1, ConversionRule::Rule_DivideBy10_1DP});
    //从排气实际过热度1
    m_addressToRuleMap.insert(161, {ui->lineEdit_Actual_exhaust_superheat1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(162, {ui->lineEdit_Actual_exhaust_superheat2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(163, {ui->lineEdit_Actual_exhaust_superheat3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(164, {ui->lineEdit_Actual_exhaust_superheat4, ConversionRule::Rule_DivideBy10_1DP});
    //回气实际过热度
    m_addressToRuleMap.insert(57, {ui->lineEdit_Return_air_superheat1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(58, {ui->lineEdit_Return_air_superheat2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(59, {ui->lineEdit_Return_air_superheat3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(60, {ui->lineEdit_Return_air_superheat4, ConversionRule::Rule_DivideBy10_1DP});
    //回气目标过热度
    m_addressToRuleMap.insert(65, {ui->lineEdit_Inhalation_target_overheating1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(66, {ui->lineEdit_Inhalation_target_overheating2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(67, {ui->lineEdit_Inhalation_target_overheating3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(68, {ui->lineEdit_Inhalation_target_overheating4, ConversionRule::Rule_DivideBy10_1DP});
    //从回气实际过热度
    m_addressToRuleMap.insert(157, {ui->lineEdit_from_Suction_superheat_1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(158, {ui->lineEdit_from_Suction_superheat_2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(159, {ui->lineEdit_from_Suction_superheat_3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(160, {ui->lineEdit_from_Suction_superheat_4, ConversionRule::Rule_DivideBy10_1DP});
    //从回气目标过热度
    m_addressToRuleMap.insert(165, {ui->lineEdit_from_Eye_catching_appeal_level_1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(166, {ui->lineEdit_from_Eye_catching_appeal_level_2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(167, {ui->lineEdit_from_Eye_catching_appeal_level_3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(168, {ui->lineEdit_from_Eye_catching_appeal_level_4, ConversionRule::Rule_DivideBy10_1DP});
    //蒸发温度1
    m_addressToRuleMap.insert(113, {ui->lineEdit_Evaporation_Temperature1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(114, {ui->lineEdit_Evaporation_Temperature2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(115, {ui->lineEdit_Evaporation_Temperature3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(116, {ui->lineEdit_Evaporation_Temperature4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //系统1高压
    m_addressToRuleMap.insert(49, {ui->lineEdit_High_Pressure_1, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(50, {ui->lineEdit_High_Pressure_2, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(51, {ui->lineEdit_Deputy_System_3_High_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(52, {ui->lineEdit_Deputy_System_4_High_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    //驱动故障代码
    m_addressToRuleMap.insert(89, {ui->lineEdit_Error_Code_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(90, {ui->lineEdit_Error_Code_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(91, {ui->lineEdit_Error_Code_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(92, {ui->lineEdit_Error_Code_4, ConversionRule::Rule_None});
    //驱动模块温度
    m_addressToRuleMap.insert(85, {ui->lineEdit_Drive_Temperature_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(86, {ui->lineEdit_Drive_Temperature_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(87, {ui->lineEdit_Drive_Temperature_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(88, {ui->lineEdit_Drive_Temperature_4, ConversionRule::Rule_None});
    //主电子膨胀阀
    m_addressToRuleMap.insert(20, {ui->lineEdit_System_1_Main_Valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(21, {ui->lineEdit_System_2_Main_Valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(24, {ui->lineEdit_Deputy_System_3_Main_Valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(25, {ui->lineEdit_Deputy_System_4_Main_Valve, ConversionRule::Rule_None});
    //辅电子膨胀阀
    m_addressToRuleMap.insert(22, {ui->lineEdit_System_1_auxiliary_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(23, {ui->lineEdit_System_2_auxiliary_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(26, {ui->lineEdit_Deputy_System_3_auxiliary_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(27, {ui->lineEdit_Deputy_System_4_auxiliary_valve, ConversionRule::Rule_None});
    //内管温度
    m_addressToRuleMap.insert(100, {ui->lineEdit_Inner_tube_1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(101, {ui->lineEdit_Inner_tube_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(102, {ui->lineEdit_Deputy_Inner_tube_3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(103, {ui->lineEdit_Deputy_Inner_tube_4, ConversionRule::Rule_DivideBy10_1DP,"℃"});

    //冷凝温度
    m_addressToRuleMap.insert(96, {ui->lineEdit_Condensing_Temperature1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(97, {ui->lineEdit_Condensing_Temperature2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(98, {ui->lineEdit_Condensing_Temperature3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(99, {ui->lineEdit_Condensing_Temperature4, ConversionRule::Rule_DivideBy10_1DP,"℃"});

    //蒸发温度1
    m_addressToRuleMap.insert(113, {ui->lineEdit_Evaporation_Temperature1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(114, {ui->lineEdit_Evaporation_Temperature2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(115, {ui->lineEdit_Evaporation_Temperature3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(116, {ui->lineEdit_Evaporation_Temperature4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //从蒸发温度1
    m_addressToRuleMap.insert(213, {ui->lineEdit_From_the_evaporation_temperature1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(214, {ui->lineEdit_From_the_evaporation_temperature2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(215, {ui->lineEdit_From_the_evaporation_temperature3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(216, {ui->lineEdit_From_the_evaporation_temperature4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //系统1分配能力
    m_addressToRuleMap.insert(3012, {ui->lineEdit_Allocation_Ability1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(3013, {ui->lineEdit_Allocation_Ability2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(3014, {ui->lineEdit_Allocation_Ability3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(3015, {ui->lineEdit_Allocation_Ability4, ConversionRule::Rule_None});
    //从机系统1分配能力
    m_addressToRuleMap.insert(4012, {ui->lineEdit_From_Allocation_Capability1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(4013, {ui->lineEdit_From_Allocation_Capability2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(4014, {ui->lineEdit_From_Allocation_Capability3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(4015, {ui->lineEdit_From_Allocation_Capability4, ConversionRule::Rule_None});

    //压缩机1电流
    m_addressToRuleMap.insert(53, {ui->lineEdit_Compressor_Current1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(54, {ui->lineEdit_Compressor_Current2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(55, {ui->lineEdit_Compressor_Current3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(56, {ui->lineEdit_Compressor_Current4, ConversionRule::Rule_None});
    //从压缩机1电流
    m_addressToRuleMap.insert(153, {ui->lineEdit_Auxiliary_compressor_current1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(154, {ui->lineEdit_Auxiliary_compressor_current2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(155, {ui->lineEdit_Auxiliary_compressor_current3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(156, {ui->lineEdit_Auxiliary_compressor_current4, ConversionRule::Rule_None});
//    m_addressToRuleMap.insert(32, {ui->lineEdit, ConversionRule::Rule_DivideBy10_1DP});

//    // 示例 2: 地址 2012 需要提取二进制第 5 位 (三参数构造函数)
//    m_addressToRuleMap.insert(2012, {ui->lineEdit, ConversionRule::Rule_ExtractBit, 5});

//    // 示例 3: 地址 43 不需要特殊处理 (默认：直接显示整数) (两参数构造函数)
//    m_addressToRuleMap.insert(43, {ui->lineEdit_3, ConversionRule::Rule_None}); // 假设 lineEdit_3 是目标控件

    //压缩机
    m_addressToRuleMap.insert(69, {ui->lineEdit_Press_Machine_Target_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(70, {ui->lineEdit_Press_Machine_Target_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(71, {ui->lineEdit_Press_Machine_Target_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(72, {ui->lineEdit_Press_Machine_Target_4, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(73, {ui->lineEdit_Press_machine_actual_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(74, {ui->lineEdit_Press_machine_actual_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(75, {ui->lineEdit_Press_machine_actual_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(76, {ui->lineEdit_Press_machine_actual_4, ConversionRule::Rule_None});

//    // 示例 4: 更多的地址...
//    m_addressToRuleMap.insert(45, {ui->lineEdit_4, ConversionRule::Rule_DivideBy10_1DP}); // 假设 lineEdit_4
//    m_addressToRuleMap.insert(46, {ui->lineEdit_5, ConversionRule::Rule_None}); // 假设 lineEdit_5
//    m_addressToRuleMap.insert(49, {ui->lineEdit_6, ConversionRule::Rule_ExtractBit, 5}); // 假设 lineEdit_6
//    // 【新增/修正点】Address 2002 必须加入 m_addressToRuleMap，以便被读取
//    // 假设 Address 2002 的值本身不需要显示，但我们必须给它指定一个 QLineEdit。
//    // 如果没有未使用的 QLineEdit，可以复用一个，或者在 UI 上添加一个隐藏的 QLineEdit。
//    // 这里假设 ui->lineEdit_10 (如果存在) 是一个可以用于占位的控件。
//    // 如果没有 ui->lineEdit_10，请将其更改为一个未被占用的 QLineEdit。
//    m_addressToRuleMap.insert(2002, {ui->lineEdit_2, ConversionRule::Rule_None}); // <-- 关键修正




//    // 需求：地址 2002 的 bit6 显示在 lineEdit_11，内容为“运行/停止”
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_20,
//                                                   ConversionRule::Rule_BitToText,
//                                                   1, "关", "开"));

//    // 需求：地址 2003 的 bit0 显示在另一个控件，内容自定义
//    m_addressToRuleMap.insert(2003, ConversionRule(ui->lineEdit_12,
//                                                   ConversionRule::Rule_BitToText,
//                                                   0, "断开", "闭合"));

    // 定义值到文本映射 (外环 1，假设 Address 40)
    QMap<quint16, QString> stateMap;
    stateMap.insert(0, tr("停机"));
    stateMap.insert(1, tr("制冷"));
    stateMap.insert(2, tr("制热"));
    stateMap.insert(3, tr("热水"));
    stateMap.insert(4, tr("除霜"));
    stateMap.insert(5, tr("热回收"));
    stateMap.insert(6, tr("待机）"));
    m_addressToRuleMap.insert(94, {
                                  ui->lineEdit_Operating_Mode,
                                  ConversionRule::Rule_ValueMap_Text,
                                  stateMap
                              });
    QMap<quint16, QString> stateMap1;
    stateMap1.insert(0, tr("停机"));
    stateMap1.insert(1, tr("制冷"));
    stateMap1.insert(2, tr("制热"));
    stateMap1.insert(3, tr("热水"));
    stateMap1.insert(4, tr("除霜"));
    stateMap1.insert(5, tr("热回收"));
    stateMap1.insert(6, tr("待机）"));
    m_addressToRuleMap.insert(194, {
                                  ui->lineEdit_from_Operating_mode,
                                  ConversionRule::Rule_ValueMap_Text,
                                  stateMap1
                              });

    QMap<quint16, QString> Refrigerant;
    Refrigerant.insert(0, tr("R410A"));
    Refrigerant.insert(1, tr("R404A"));
    Refrigerant.insert(2, tr("R32"));
    m_addressToRuleMap.insert(1756, {
                                  ui->lineEdit_Refrigerant,
                                  ConversionRule::Rule_ValueMap_Text,
                                  Refrigerant
                              });



//    // Address 32 (外环 4) 使用 Rule_DivideBy10_1DP，需要 '-' 前缀
//    m_addressToRuleMap.insert(32, {
//                                  ui->lineEdit_8,
//                                  ConversionRule::Rule_DivideBy10_1DP,
//                                  "-",
//                                  ""
//                              });
//    // Address 55 (外环 5)
//    m_addressToRuleMap.insert(55, {
//                                  ui->lineEdit_9,
//                                  ConversionRule::Rule_DivideBy10_1DP,
//                                  "-",
//                                  "℃"
//                              });

    // =======================================================
    // 从机
    // =======================================================

//    m_addressToRuleMap.insert(2010, {ui->lineEdit_from_Refrigerant, ConversionRule::Rule_None});
        QMap<quint16, QString> Refrigerant1;
        Refrigerant1.insert(0, tr("R410A"));
        Refrigerant1.insert(1, tr("R404A"));
        Refrigerant1.insert(2, tr("R32"));
        m_addressToRuleMap.insert(11756, {
                                      ui->lineEdit_from_Refrigerant,
                                      ConversionRule::Rule_ValueMap_Text,
                                      Refrigerant1
                                  });

    //低压
    m_addressToRuleMap.insert(145, {ui->lineEdit_from_System_1_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(146, {ui->lineEdit_from_System_2_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(147, {ui->lineEdit_from_System_3_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    m_addressToRuleMap.insert(148, {ui->lineEdit_from_System_4_Low_Pressure, ConversionRule::Rule_DivideBy10_1DP,"Bar"});
    //吸目过热度
    m_addressToRuleMap.insert(165, {ui->lineEdit_from_Eye_catching_appeal_level_1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(166, {ui->lineEdit_from_Eye_catching_appeal_level_2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(167, {ui->lineEdit_from_Eye_catching_appeal_level_3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(168, {ui->lineEdit_from_Eye_catching_appeal_level_4, ConversionRule::Rule_DivideBy10_1DP});
    //吸实过热度
    m_addressToRuleMap.insert(157, {ui->lineEdit_from_Suction_superheat_1, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(158, {ui->lineEdit_from_Suction_superheat_2, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(159, {ui->lineEdit_from_Suction_superheat_3, ConversionRule::Rule_DivideBy10_1DP});
    m_addressToRuleMap.insert(160, {ui->lineEdit_from_Suction_superheat_4, ConversionRule::Rule_DivideBy10_1DP});
    //程序版本
    m_addressToRuleMap.insert(195, {ui->lineEdit_from_Program_version, ConversionRule::Rule_DivideBy10_1DP});
    //风机驱动代号
    m_addressToRuleMap.insert(3008, {ui->lineEdit_Exhaust_10, ConversionRule::Rule_DivideBy10_1DP});
    //压缩机驱动代号
    m_addressToRuleMap.insert(3007, {ui->lineEdit_Exhaust_9, ConversionRule::Rule_DivideBy10_1DP});
    //排气
    m_addressToRuleMap.insert(128, {ui->lineEdit_from_Exhaust_1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(129, {ui->lineEdit_from_Exhaust_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(130, {ui->lineEdit_from_Exhaust_3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(131, {ui->lineEdit_from_Exhaust_4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //环境温度
    m_addressToRuleMap.insert(134, {ui->lineEdit_from_Ambient_temperature, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //进水
    m_addressToRuleMap.insert(132, {ui->lineEdit_from_Water_inlet, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //出水
    m_addressToRuleMap.insert(133, {ui->lineEdit_from_Water_outlet, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //盘管
    m_addressToRuleMap.insert(139, {ui->lineEdit_from_Coil_1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(140, {ui->lineEdit_from_Coil_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(141, {ui->lineEdit_from_Coil_3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(142, {ui->lineEdit_from_Coil_4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //冷凝温度
    m_addressToRuleMap.insert(196, {ui->lineEdit_from_Condensation_temperature1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(197, {ui->lineEdit_from_Condensation_temperature2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(198, {ui->lineEdit_from_Condensation_temperature3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(199, {ui->lineEdit_from_Condensation_temperature4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //内管
    m_addressToRuleMap.insert(200, {ui->lineEdit_from_Inner_tube_1, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(201, {ui->lineEdit_from_Inner_tube_2, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(202, {ui->lineEdit_from_Inner_tube_3, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(203, {ui->lineEdit_from_Inner_tube_4, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //从机系统高压
    m_addressToRuleMap.insert(149, {ui->lineEdit_from_System_1_high_pressure, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(150, {ui->lineEdit_from_System_2_high_pressure, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(151, {ui->lineEdit_from_System_3_high_pressure, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    m_addressToRuleMap.insert(152, {ui->lineEdit_from_System_4_high_pressure, ConversionRule::Rule_DivideBy10_1DP,"℃"});
    //系统主阀
    m_addressToRuleMap.insert(120, {ui->lineEdit_from_System_1_main_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(121, {ui->lineEdit_from_System_2_main_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(124, {ui->lineEdit_from_System_3_main_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(125, {ui->lineEdit_from_System_4_main_valve, ConversionRule::Rule_None});
    //4WV1~4WV4
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_Auxiliary_heat,ConversionRule::Rule_BitToText,0, "开启", "关闭"));
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_Auxiliary_heat,ConversionRule::Rule_BitToText,0, "开启", "关闭"));
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_Auxiliary_heat,ConversionRule::Rule_BitToText,0, "开启", "关闭"));
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_Auxiliary_heat,ConversionRule::Rule_BitToText,0, "开启", "关闭"));
    //辅热
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_from_Auxiliary_heating,ConversionRule::Rule_BitToText,1, "关", "开"));
    //从机风机设置
    m_addressToRuleMap.insert(177, {ui->lineEdit_from_Fan_setting_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(178, {ui->lineEdit_from_Fan_setting_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(179, {ui->lineEdit_from_Fan_setting_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(180, {ui->lineEdit_from_Fan_setting_4, ConversionRule::Rule_None});
    //风机实际
    m_addressToRuleMap.insert(181, {ui->lineEdit_from_Fan_actual_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(182, {ui->lineEdit_from_Fan_actual_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(183, {ui->lineEdit_from_Fan_actual_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(184, {ui->lineEdit_from_Fan_actual_4, ConversionRule::Rule_None});
    //风机档位
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_from_Auxiliary_heating,ConversionRule::Rule_BitToText,1, "停机", "开启"));
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_from_Auxiliary_heating,ConversionRule::Rule_BitToText,1, "停机", "开启"));
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_from_Auxiliary_heating,ConversionRule::Rule_BitToText,1, "停机", "开启"));
//    m_addressToRuleMap.insert(3009, ConversionRule(ui->lineEdit_from_Auxiliary_heating,ConversionRule::Rule_BitToText,1, "停机", "开启"));
    //从机系统1铺阀
    m_addressToRuleMap.insert(122, {ui->lineEdit_from_System_1_auxiliary_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(123, {ui->lineEdit_from_System_2_auxiliary_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(126, {ui->lineEdit_from_System_3_auxiliary_valve, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(127, {ui->lineEdit_from_System_4_auxiliary_valve, ConversionRule::Rule_None});
    //从机压机目标
    m_addressToRuleMap.insert(169, {ui->lineEdit_from_Press_target_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(170, {ui->lineEdit_from_Press_target_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(171, {ui->lineEdit_from_Press_target_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(172, {ui->lineEdit_from_Press_target_4, ConversionRule::Rule_None});
    //从机压机实际
    m_addressToRuleMap.insert(173, {ui->lineEdit_from_Press_machine_actual_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(174, {ui->lineEdit_from_Press_machine_actual_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(175, {ui->lineEdit_from_Press_machine_actual_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(176, {ui->lineEdit_from_Press_machine_actual_4, ConversionRule::Rule_None});
    //故障代码
    m_addressToRuleMap.insert(189, {ui->lineEdit_from_Error_code_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(190, {ui->lineEdit_from_Error_code_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(191, {ui->lineEdit_from_Error_code_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(192, {ui->lineEdit_from_Error_code_4, ConversionRule::Rule_None});
    //驱动温度
    m_addressToRuleMap.insert(185, {ui->lineEdit_from_Driving_temperature_1, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(186, {ui->lineEdit_from_Driving_temperature_2, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(187, {ui->lineEdit_from_Driving_temperature_3, ConversionRule::Rule_None});
    m_addressToRuleMap.insert(188, {ui->lineEdit_from_Driving_temperature_4, ConversionRule::Rule_None});


    // =======================================================
    // II. 纯故障监控地址 (m_addressToMonitorMap)
    // =======================================================
    // Address 2001：
    QList<FaultBitDefinition> faults2001;
    faults2001.append({0, tr("压缩机1保护"), 1});
    faults2001.append({1, tr("压缩机2保护"), 1});
    faults2001.append({2, tr("水泵过载"), 1});
    faults2001.append({3, tr("水流开关保护"), 1});
    faults2001.append({4, tr("系统1高压故障"), 1});
    faults2001.append({5, tr("系统1低压故障"), 1});
    faults2001.append({6, tr("保留"), 1});
    faults2001.append({7, tr("系统1风机故障"), 1});
    faults2001.append({8, tr("系统2高压故障"), 1});
    faults2001.append({9, tr("出水温度过低保护"), 1});
    faults2001.append({10, tr("电加热过载"), 1});
    faults2001.append({11, tr("环境温度过高/过低"), 1});
    faults2001.append({12, tr("与风机驱动器1通讯故障"), 1});
    faults2001.append({13, tr("系统1过热度过小保护"), 1});
    faults2001.append({14, tr("系统2风机故障"), 1});
    m_addressToMonitorMap.insert(2001, faults2001);

    // Address 2002：
    QList<FaultBitDefinition> faults2002;
    faults2002.append({0, tr("系统1回气温度过高"), 1});
    faults2002.append({1, tr("系统1排气温度过高"), 1});
    faults2002.append({2, tr("系统2回气温度过高"), 1});
    faults2002.append({3, tr("系统2排气温度过高"), 1});
    faults2002.append({4, tr("系统2低压故障"), 1});
    faults2002.append({5, tr("系统2过热度过小保护"), 1});
    faults2002.append({6, tr("系统1冷媒泄露"), 1});
    faults2002.append({7, tr("系统2冷媒泄露"), 1});
    faults2002.append({8, tr("排气温度1传感器故障"), 1});
    faults2002.append({9, tr("排气温度2传感器故障"), 1});
    faults2002.append({10, tr("盘管1温度传感器故障"), 1});
    faults2002.append({11, tr("盘管2温度传感器故障"), 1});
    faults2002.append({12, tr("总管温度传感器故障"), 1});
    faults2002.append({13, tr("进水温度温度传感器故障"), 1});
    faults2002.append({14, tr("出水温度温度传感器故障"), 1});
    faults2002.append({15, tr("环境温度传感器故障"), 1});
    m_addressToMonitorMap.insert(2002, faults2002);

    // Address 2002：
    QList<FaultBitDefinition> faults2003;
    faults2003.append({0, tr("回气1温度传感器故障"), 1});
    faults2003.append({1, tr("回气2温度传感器故障"), 1});
    faults2003.append({2, tr("补气1进口温度传感器故障"), 1});
    faults2003.append({3, tr("补气2进口温度传感器故障"), 1});
    faults2003.append({4, tr("低压传感器2故障"), 1});
    faults2003.append({5, tr("低压传感器1故障"), 1});
    faults2003.append({6, tr("保留"), 1});
    faults2003.append({7, tr("四通阀1换向故障"), 1});
    faults2003.append({8, tr("补气1出口温度传感器故障"), 1});
    faults2003.append({9, tr("出水温度过高"), 1});
    faults2003.append({10, tr("系统1电流互感器故障"), 1});
    faults2003.append({11, tr("系统2电流互感器故障"), 1});
    faults2003.append({12, tr("保留"), 1});
    faults2003.append({13, tr("温差过大"), 1});
    faults2003.append({14, tr("四通阀2换向故障"), 1});
    faults2003.append({15, tr("补气2出口温度传感器故障"), 1});
    m_addressToMonitorMap.insert(2003, faults2003);

    // Address 2004：
    QList<FaultBitDefinition> faults2004;
    faults2004.append({0, tr("高压传感器1故障"), 1});
    faults2004.append({1, tr("高压传感器2故障"), 1});
    faults2004.append({2, tr("压缩机通讯故障1"), 1});
    faults2004.append({3, tr("压缩机通讯故障2"), 1});
    faults2004.append({4, tr("保留"), 1});
    faults2004.append({5, tr("相序故障"), 1});
    faults2004.append({6, tr("保留"), 1});
    faults2004.append({7, tr("与风机驱动器2通讯故障"), 1});
    faults2004.append({8, tr("蒸发温度传感器1故障"), 1});
    faults2004.append({9, tr("蒸发温度传感器2故障"), 1});
    faults2004.append({10, tr("压缩机1电源故障"), 1});
    faults2004.append({11, tr("压缩机2电源故障"), 1});
    faults2004.append({12, tr("风机1电源故障"), 1});
    faults2004.append({13, tr("风机2电源故障"), 1});
    faults2004.append({14, tr("保留"), 1});
    faults2004.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2004, faults2004);

    // Address 2005：
    QList<FaultBitDefinition> faults2005;
    faults2005.append({0, tr("压缩机3保护"), 1});
    faults2005.append({1, tr("压缩机4保护"), 1});
    faults2005.append({4, tr("系统3高压故障"), 1});
    faults2005.append({5, tr("系统3低压故障"), 1});
    faults2005.append({6, tr("保留"), 1});
    faults2005.append({7, tr("系统3风机故障"), 1});
    faults2005.append({8, tr("系统4高压故障"), 1});
    faults2005.append({12, tr("与风机驱动器3通讯故障"), 1});
    faults2005.append({13, tr("系统3过热度过小保护"), 1});
    faults2005.append({14, tr("系统4风机故障"), 1});
    faults2005.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2005, faults2005);

    // Address 2006：
    QList<FaultBitDefinition> faults2006;
    faults2006.append({0, tr("系统3回气温度过高"), 1});
    faults2006.append({1, tr("系统3排气温度过高"), 1});
    faults2006.append({2, tr("系统4回气温度过高"), 1});
    faults2006.append({3, tr("系统4排气温度过高"), 1});
    faults2006.append({4, tr("系统4低压故障"), 1});
    faults2006.append({5, tr("系统4过热度过小保护"), 1});
    faults2006.append({6, tr("系统3冷媒泄露"), 1});
    faults2006.append({7, tr("系统4冷媒泄露"), 1});
    faults2006.append({8, tr("排气温度3传感器故障"), 1});
    faults2006.append({9, tr("排气温度4传感器故障"), 1});
    faults2006.append({10, tr("盘管3温度传感器故障"), 1});
    faults2006.append({11, tr("盘管4温度传感器故障"), 1});
    faults2006.append({12, tr("保留"), 1});
    faults2006.append({13, tr("保留"), 1});
    faults2006.append({14, tr("保留"), 1});
    faults2006.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2006, faults2006);

    // Address 2007：
    QList<FaultBitDefinition> faults2007;
    faults2007.append({0, tr("回气3温度传感器故障"), 1});
    faults2007.append({1, tr("回气4温度传感器故障"), 1});
    faults2007.append({2, tr("补气3进口温度传感器故障"), 1});
    faults2007.append({3, tr("补气4进口温度传感器故障"), 1});
    faults2007.append({4, tr("低压传感器4故障"), 1});
    faults2007.append({5, tr("低压传感器3故障"), 1});
    faults2007.append({6, tr("保留"), 1});
    faults2007.append({8, tr("补气3出口温度传感器故障"), 1});
    faults2007.append({10, tr("保留"), 1});
    faults2007.append({11, tr("保留"), 1});
    faults2007.append({12, tr("保留"), 1});
    faults2007.append({13, tr("保留"), 1});
    faults2007.append({15, tr("补气4出口温度传感器故障"), 1});
    m_addressToMonitorMap.insert(2007, faults2007);

    // Address 2006：
    QList<FaultBitDefinition> faults2008;
    faults2008.append({0, tr("高压传感器3故障"), 1});
    faults2008.append({1, tr("高压传感器4故障"), 1});
    faults2008.append({2, tr("压缩机通讯故障3"), 1});
    faults2008.append({3, tr("压缩机通讯故障4"), 1});
    faults2008.append({4, tr("保留"), 1});
    faults2008.append({5, tr("保留"), 1});
    faults2008.append({6, tr("保留"), 1});
    faults2008.append({7, tr("与风机驱动器4通讯故障"), 1});
    faults2008.append({8, tr("蒸发温度传感器3故障"), 1});
    faults2008.append({9, tr("蒸发温度传感器4故障"), 1});
    faults2008.append({10, tr("压缩机3电源故障"), 1});
    faults2008.append({11, tr("压缩机4电源故障"), 1});
    faults2008.append({12, tr("风机3电源故障"), 1});
    faults2008.append({13, tr("风机4电源故障"), 1});
    faults2008.append({14, tr("保留"), 1});
    faults2008.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2008, faults2008);

    // =======================================================
    // II. 纯故障监控地址 (m_addressToMonitorMap)
    //从机
    // =======================================================
    // Address 2001：
    QList<FaultBitDefinition> faults2009;
    faults2001.append({0, tr("压缩机1保护"), 1});
    faults2001.append({1, tr("压缩机2保护"), 1});
    faults2001.append({2, tr("水泵过载"), 1});
    faults2001.append({3, tr("水流开关保护"), 1});
    faults2001.append({4, tr("系统1高压故障"), 1});
    faults2001.append({5, tr("系统1低压故障"), 1});
    faults2001.append({6, tr("保留"), 1});
    faults2001.append({7, tr("系统1风机故障"), 1});
    faults2001.append({8, tr("系统2高压故障"), 1});
    faults2001.append({9, tr("出水温度过低保护"), 1});
    faults2001.append({10, tr("电加热过载"), 1});
    faults2001.append({11, tr("环境温度过高/过低"), 1});
    faults2001.append({12, tr("与风机驱动器1通讯故障"), 1});
    faults2001.append({13, tr("系统1过热度过小保护"), 1});
    faults2001.append({14, tr("系统2风机故障"), 1});
    m_addressToMonitorMap.insert(2009, faults2009);

    // Address 2002：
    QList<FaultBitDefinition> faults2010;
    faults2002.append({0, tr("系统1回气温度过高"), 1});
    faults2002.append({1, tr("系统1排气温度过高"), 1});
    faults2002.append({2, tr("系统2回气温度过高"), 1});
    faults2002.append({3, tr("系统2排气温度过高"), 1});
    faults2002.append({4, tr("系统2低压故障"), 1});
    faults2002.append({5, tr("系统2过热度过小保护"), 1});
    faults2002.append({6, tr("系统1冷媒泄露"), 1});
    faults2002.append({7, tr("系统2冷媒泄露"), 1});
    faults2002.append({8, tr("排气温度1传感器故障"), 1});
    faults2002.append({9, tr("排气温度2传感器故障"), 1});
    faults2002.append({10, tr("盘管1温度传感器故障"), 1});
    faults2002.append({11, tr("盘管2温度传感器故障"), 1});
    faults2002.append({12, tr("总管温度传感器故障"), 1});
    faults2002.append({13, tr("进水温度温度传感器故障"), 1});
    faults2002.append({14, tr("出水温度温度传感器故障"), 1});
    faults2002.append({15, tr("环境温度传感器故障"), 1});
    m_addressToMonitorMap.insert(2010, faults2010);

    // Address 2002：
    QList<FaultBitDefinition> faults2011;
    faults2003.append({0, tr("回气1温度传感器故障"), 1});
    faults2003.append({1, tr("回气2温度传感器故障"), 1});
    faults2003.append({2, tr("补气1进口温度传感器故障"), 1});
    faults2003.append({3, tr("补气2进口温度传感器故障"), 1});
    faults2003.append({4, tr("低压传感器2故障"), 1});
    faults2003.append({5, tr("低压传感器1故障"), 1});
    faults2003.append({6, tr("保留"), 1});
    faults2003.append({7, tr("四通阀1换向故障"), 1});
    faults2003.append({8, tr("补气1出口温度传感器故障"), 1});
    faults2003.append({9, tr("出水温度过高"), 1});
    faults2003.append({10, tr("系统1电流互感器故障"), 1});
    faults2003.append({11, tr("系统2电流互感器故障"), 1});
    faults2003.append({12, tr("保留"), 1});
    faults2003.append({13, tr("温差过大"), 1});
    faults2003.append({14, tr("四通阀2换向故障"), 1});
    faults2003.append({15, tr("补气2出口温度传感器故障"), 1});
    m_addressToMonitorMap.insert(2011, faults2011);

    // Address 2004：
    QList<FaultBitDefinition> faults2012;
    faults2004.append({0, tr("高压传感器1故障"), 1});
    faults2004.append({1, tr("高压传感器2故障"), 1});
    faults2004.append({2, tr("压缩机通讯故障1"), 1});
    faults2004.append({3, tr("压缩机通讯故障2"), 1});
    faults2004.append({4, tr("保留"), 1});
    faults2004.append({5, tr("相序故障"), 1});
    faults2004.append({6, tr("保留"), 1});
    faults2004.append({7, tr("与风机驱动器2通讯故障"), 1});
    faults2004.append({8, tr("蒸发温度传感器1故障"), 1});
    faults2004.append({9, tr("蒸发温度传感器2故障"), 1});
    faults2004.append({10, tr("压缩机1电源故障"), 1});
    faults2004.append({11, tr("压缩机2电源故障"), 1});
    faults2004.append({12, tr("风机1电源故障"), 1});
    faults2004.append({13, tr("风机2电源故障"), 1});
    faults2004.append({14, tr("保留"), 1});
    faults2004.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2012, faults2012);

    // Address 2005：
    QList<FaultBitDefinition> faults2013;
    faults2005.append({0, tr("压缩机3保护"), 1});
    faults2005.append({1, tr("压缩机4保护"), 1});
    faults2005.append({4, tr("系统3高压故障"), 1});
    faults2005.append({5, tr("系统3低压故障"), 1});
    faults2005.append({6, tr("保留"), 1});
    faults2005.append({7, tr("系统3风机故障"), 1});
    faults2005.append({8, tr("系统4高压故障"), 1});
    faults2005.append({12, tr("与风机驱动器3通讯故障"), 1});
    faults2005.append({13, tr("系统3过热度过小保护"), 1});
    faults2005.append({14, tr("系统4风机故障"), 1});
    faults2005.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2013, faults2013);

    // Address 2006：
    QList<FaultBitDefinition> faults2014;
    faults2006.append({0, tr("系统3回气温度过高"), 1});
    faults2006.append({1, tr("系统3排气温度过高"), 1});
    faults2006.append({2, tr("系统4回气温度过高"), 1});
    faults2006.append({3, tr("系统4排气温度过高"), 1});
    faults2006.append({4, tr("系统4低压故障"), 1});
    faults2006.append({5, tr("系统4过热度过小保护"), 1});
    faults2006.append({6, tr("系统3冷媒泄露"), 1});
    faults2006.append({7, tr("系统4冷媒泄露"), 1});
    faults2006.append({8, tr("排气温度3传感器故障"), 1});
    faults2006.append({9, tr("排气温度4传感器故障"), 1});
    faults2006.append({10, tr("盘管3温度传感器故障"), 1});
    faults2006.append({11, tr("盘管4温度传感器故障"), 1});
    faults2006.append({12, tr("保留"), 1});
    faults2006.append({13, tr("保留"), 1});
    faults2006.append({14, tr("保留"), 1});
    faults2006.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2014, faults2014);

    // Address 2007：
    QList<FaultBitDefinition> faults2015;
    faults2007.append({0, tr("回气3温度传感器故障"), 1});
    faults2007.append({1, tr("回气4温度传感器故障"), 1});
    faults2007.append({2, tr("补气3进口温度传感器故障"), 1});
    faults2007.append({3, tr("补气4进口温度传感器故障"), 1});
    faults2007.append({4, tr("低压传感器4故障"), 1});
    faults2007.append({5, tr("低压传感器3故障"), 1});
    faults2007.append({6, tr("保留"), 1});
    faults2007.append({8, tr("补气3出口温度传感器故障"), 1});
    faults2007.append({10, tr("保留"), 1});
    faults2007.append({11, tr("保留"), 1});
    faults2007.append({12, tr("保留"), 1});
    faults2007.append({13, tr("保留"), 1});
    faults2007.append({15, tr("补气4出口温度传感器故障"), 1});
    m_addressToMonitorMap.insert(2015, faults2015);

    // Address 2006：
    QList<FaultBitDefinition> faults2016;
    faults2008.append({0, tr("高压传感器3故障"), 1});
    faults2008.append({1, tr("高压传感器4故障"), 1});
    faults2008.append({2, tr("压缩机通讯故障3"), 1});
    faults2008.append({3, tr("压缩机通讯故障4"), 1});
    faults2008.append({4, tr("保留"), 1});
    faults2008.append({5, tr("保留"), 1});
    faults2008.append({6, tr("保留"), 1});
    faults2008.append({7, tr("与风机驱动器4通讯故障"), 1});
    faults2008.append({8, tr("蒸发温度传感器3故障"), 1});
    faults2008.append({9, tr("蒸发温度传感器4故障"), 1});
    faults2008.append({10, tr("压缩机3电源故障"), 1});
    faults2008.append({11, tr("压缩机4电源故障"), 1});
    faults2008.append({12, tr("风机3电源故障"), 1});
    faults2008.append({13, tr("风机4电源故障"), 1});
    faults2008.append({14, tr("保留"), 1});
    faults2008.append({15, tr("保留"), 1});
    m_addressToMonitorMap.insert(2016, faults2016);

    // 曲线可选数据项：Modbus 地址 -> 中文名称（与数据页 label 对应，供曲线配置选择）
    m_curveDisplayNames.clear();
    m_curveDisplayNames.insert(1, tr("总出水温度"));
    m_curveDisplayNames.insert(32, tr("进水温度"));
    m_curveDisplayNames.insert(33, tr("出水温度"));
    m_curveDisplayNames.insert(34, tr("环境温度"));
    m_curveDisplayNames.insert(35, tr("吸气1"));
    m_curveDisplayNames.insert(36, tr("吸气2"));
    m_curveDisplayNames.insert(37, tr("吸气3"));
    m_curveDisplayNames.insert(38, tr("吸气4"));
    m_curveDisplayNames.insert(28, tr("排气1"));
    m_curveDisplayNames.insert(29, tr("排气2"));
    m_curveDisplayNames.insert(30, tr("排气3"));
    m_curveDisplayNames.insert(31, tr("排气4"));
    m_curveDisplayNames.insert(45, tr("系统1低压"));
    m_curveDisplayNames.insert(46, tr("系统2低压"));
    m_curveDisplayNames.insert(47, tr("系统3低压"));
    m_curveDisplayNames.insert(48, tr("系统4低压"));
    m_curveDisplayNames.insert(49, tr("高压1"));
    m_curveDisplayNames.insert(50, tr("高压2"));
    m_curveDisplayNames.insert(51, tr("系统3高压"));
    m_curveDisplayNames.insert(52, tr("系统4高压"));
    m_curveDisplayNames.insert(39, tr("盘管1"));
    m_curveDisplayNames.insert(40, tr("盘管2"));
    m_curveDisplayNames.insert(41, tr("盘管3"));
    m_curveDisplayNames.insert(42, tr("盘管4"));
    m_curveDisplayNames.insert(43, tr("排目过热度1"));
    m_curveDisplayNames.insert(53, tr("压缩机1电流"));
    m_curveDisplayNames.insert(54, tr("压缩机2电流"));
    m_curveDisplayNames.insert(55, tr("压缩机3电流"));
    m_curveDisplayNames.insert(56, tr("压缩机4电流"));
    m_curveDisplayNames.insert(57, tr("吸气实际过热度1"));
    m_curveDisplayNames.insert(58, tr("吸气实际过热度2"));
    m_curveDisplayNames.insert(59, tr("吸气实际过热度3"));
    m_curveDisplayNames.insert(60, tr("吸气实际过热度4"));
    m_curveDisplayNames.insert(61, tr("排气实际过热度1"));
    m_curveDisplayNames.insert(62, tr("排气实际过热度2"));
    m_curveDisplayNames.insert(63, tr("排气实际过热度3"));
    m_curveDisplayNames.insert(64, tr("排气实际过热度4"));
    m_curveDisplayNames.insert(65, tr("吸目过热度1"));
    m_curveDisplayNames.insert(66, tr("吸目过热度2"));
    m_curveDisplayNames.insert(67, tr("吸目过热度3"));
    m_curveDisplayNames.insert(68, tr("吸目过热度4"));
    m_curveDisplayNames.insert(85, tr("驱动温度1"));
    m_curveDisplayNames.insert(86, tr("驱动温度2"));
    m_curveDisplayNames.insert(87, tr("驱动温度3"));
    m_curveDisplayNames.insert(88, tr("驱动温度4"));
    m_curveDisplayNames.insert(96, tr("冷凝温度1"));
    m_curveDisplayNames.insert(97, tr("冷凝温度2"));
    m_curveDisplayNames.insert(98, tr("冷凝温度3"));
    m_curveDisplayNames.insert(99, tr("冷凝温度4"));
    m_curveDisplayNames.insert(100, tr("内管1"));
    m_curveDisplayNames.insert(101, tr("内管2"));
    m_curveDisplayNames.insert(102, tr("内管3"));
    m_curveDisplayNames.insert(103, tr("内管4"));
    m_curveDisplayNames.insert(113, tr("蒸发温度1"));
    m_curveDisplayNames.insert(114, tr("蒸发温度2"));
    m_curveDisplayNames.insert(115, tr("蒸发温度3"));
    m_curveDisplayNames.insert(116, tr("蒸发温度4"));
    m_curveDisplayNames.insert(53, tr("压缩机1电流"));
    m_curveDisplayNames.insert(54, tr("压缩机2电流"));
    m_curveDisplayNames.insert(55, tr("压缩机3电流"));
    m_curveDisplayNames.insert(56, tr("压缩机4电流"));

    // 主阀/辅助阀（原显示为“地址20”等）
    m_curveDisplayNames.insert(20, tr("系统1主阀"));
    m_curveDisplayNames.insert(21, tr("系统2主阀"));
    m_curveDisplayNames.insert(22, tr("系统1辅助阀"));
    m_curveDisplayNames.insert(23, tr("系统2辅助阀"));
    m_curveDisplayNames.insert(24, tr("系统3主阀"));
    m_curveDisplayNames.insert(25, tr("系统4主阀"));
    m_curveDisplayNames.insert(26, tr("系统3辅助阀"));
    m_curveDisplayNames.insert(27, tr("系统4辅助阀"));
    // 压机目标/实际
    m_curveDisplayNames.insert(69, tr("压机目标1"));
    m_curveDisplayNames.insert(70, tr("压机目标2"));
    m_curveDisplayNames.insert(71, tr("压机目标3"));
    m_curveDisplayNames.insert(72, tr("压机目标4"));
    m_curveDisplayNames.insert(73, tr("压机实际1"));
    m_curveDisplayNames.insert(74, tr("压机实际2"));
    m_curveDisplayNames.insert(75, tr("压机实际3"));
    m_curveDisplayNames.insert(76, tr("压机实际4"));
    // 风机设定/实际
    m_curveDisplayNames.insert(77, tr("风机设定1"));
    m_curveDisplayNames.insert(78, tr("风机设定2"));
    m_curveDisplayNames.insert(79, tr("风机设定3"));
    m_curveDisplayNames.insert(80, tr("风机设定4"));
    m_curveDisplayNames.insert(81, tr("风机实际1"));
    m_curveDisplayNames.insert(82, tr("风机实际2"));
    m_curveDisplayNames.insert(83, tr("风机实际3"));
    m_curveDisplayNames.insert(84, tr("风机实际4"));
    // 故障码
    m_curveDisplayNames.insert(89, tr("故障代码1"));
    m_curveDisplayNames.insert(90, tr("故障代码2"));
    m_curveDisplayNames.insert(91, tr("故障代码3"));
    m_curveDisplayNames.insert(92, tr("故障代码4"));
    // 制冷剂类型
    m_curveDisplayNames.insert(1756, tr("冷煤"));
    m_curveDisplayNames.insert(94, tr("运行模式"));
    // 程序版本号
    m_curveDisplayNames.insert(95, tr("程序版本"));
    m_curveDisplayNames.insert(3003, tr("无霜环翅温差1"));
    m_curveDisplayNames.insert(3004, tr("无霜环翅温差2"));
    m_curveDisplayNames.insert(3005, tr("无霜环翅温差3"));
    m_curveDisplayNames.insert(3006, tr("无霜环翅温差4"));
    m_curveDisplayNames.insert(3007, tr("压缩机代号"));
    m_curveDisplayNames.insert(3008, tr("风机代号"));
    m_curveDisplayNames.insert(3012, tr("分配能力1"));
    m_curveDisplayNames.insert(3013, tr("分配能力2"));
    m_curveDisplayNames.insert(3014, tr("分配能力3"));
    m_curveDisplayNames.insert(3015, tr("分配能力4"));

    // 从机相关（与 lineEdit_from_* 对应）
    m_curveDisplayNames.insert(11756, tr("从机冷媒"));
    m_curveDisplayNames.insert(120, tr("从机系统1主阀"));
    m_curveDisplayNames.insert(121, tr("从机系统2主阀"));
    m_curveDisplayNames.insert(122, tr("从机系统1辅阀"));
    m_curveDisplayNames.insert(123, tr("从机系统2辅阀"));
    m_curveDisplayNames.insert(124, tr("从机系统3主阀"));
    m_curveDisplayNames.insert(125, tr("从机系统4主阀"));
    m_curveDisplayNames.insert(126, tr("从机系统3辅阀"));
    m_curveDisplayNames.insert(127, tr("从机系统4辅阀"));
    m_curveDisplayNames.insert(128, tr("从机排气1"));
    m_curveDisplayNames.insert(129, tr("从机排气2"));
    m_curveDisplayNames.insert(130, tr("从机排气3"));
    m_curveDisplayNames.insert(131, tr("从机排气4"));
    m_curveDisplayNames.insert(132, tr("从机进水温度"));
    m_curveDisplayNames.insert(133, tr("从机出水温度"));
    m_curveDisplayNames.insert(134, tr("从机环境温度"));
    m_curveDisplayNames.insert(135, tr("从机吸气1"));
    m_curveDisplayNames.insert(136, tr("从机吸气2"));
    m_curveDisplayNames.insert(137, tr("从机吸气3"));
    m_curveDisplayNames.insert(138, tr("从机吸气4"));
    m_curveDisplayNames.insert(139, tr("从机盘管1"));
    m_curveDisplayNames.insert(140, tr("从机盘管2"));
    m_curveDisplayNames.insert(141, tr("从机盘管3"));
    m_curveDisplayNames.insert(142, tr("从机盘管4"));
    m_curveDisplayNames.insert(143, tr("从机排目过热度1"));
    m_curveDisplayNames.insert(145, tr("从机系统1低压"));
    m_curveDisplayNames.insert(146, tr("从机系统2低压"));
    m_curveDisplayNames.insert(147, tr("从机系统3低压"));
    m_curveDisplayNames.insert(148, tr("从机系统4低压"));
    m_curveDisplayNames.insert(149, tr("从机系统1高压"));
    m_curveDisplayNames.insert(150, tr("从机系统2高压"));
    m_curveDisplayNames.insert(151, tr("从机系统3高压"));
    m_curveDisplayNames.insert(152, tr("从机系统4高压"));
    m_curveDisplayNames.insert(153, tr("从机压缩机1电流"));
    m_curveDisplayNames.insert(154, tr("从机压缩机2电流"));
    m_curveDisplayNames.insert(155, tr("从机压缩机3电流"));
    m_curveDisplayNames.insert(156, tr("从机压缩机4电流"));
    m_curveDisplayNames.insert(157, tr("从机吸气过热度1"));
    m_curveDisplayNames.insert(158, tr("从机吸气过热度2"));
    m_curveDisplayNames.insert(159, tr("从机吸气过热度3"));
    m_curveDisplayNames.insert(160, tr("从机吸气过热度4"));
    m_curveDisplayNames.insert(161, tr("从机排实过热度1"));
    m_curveDisplayNames.insert(162, tr("从机排实过热度2"));
    m_curveDisplayNames.insert(163, tr("从机排实过热度3"));
    m_curveDisplayNames.insert(164, tr("从机排实过热度4"));
    m_curveDisplayNames.insert(165, tr("从机吸目过热度1"));
    m_curveDisplayNames.insert(166, tr("从机吸目过热度2"));
    m_curveDisplayNames.insert(167, tr("从机吸目过热度3"));
    m_curveDisplayNames.insert(168, tr("从机吸目过热度4"));
    m_curveDisplayNames.insert(169, tr("从机压机目标1"));
    m_curveDisplayNames.insert(170, tr("从机压机目标2"));
    m_curveDisplayNames.insert(171, tr("从机压机目标3"));
    m_curveDisplayNames.insert(172, tr("从机压机目标4"));
    m_curveDisplayNames.insert(173, tr("从机压机实际1"));
    m_curveDisplayNames.insert(174, tr("从机压机实际2"));
    m_curveDisplayNames.insert(175, tr("从机压机实际3"));
    m_curveDisplayNames.insert(176, tr("从机压机实际4"));
    m_curveDisplayNames.insert(177, tr("从机风机设置1"));
    m_curveDisplayNames.insert(178, tr("从机风机设置2"));
    m_curveDisplayNames.insert(179, tr("从机风机设置3"));
    m_curveDisplayNames.insert(180, tr("从机风机设置4"));
    m_curveDisplayNames.insert(181, tr("从机风机实际1"));
    m_curveDisplayNames.insert(182, tr("从机风机实际2"));
    m_curveDisplayNames.insert(183, tr("从机风机实际3"));
    m_curveDisplayNames.insert(184, tr("从机风机实际4"));
    m_curveDisplayNames.insert(185, tr("从机驱动温度1"));
    m_curveDisplayNames.insert(186, tr("从机驱动温度2"));
    m_curveDisplayNames.insert(187, tr("从机驱动温度3"));
    m_curveDisplayNames.insert(188, tr("从机驱动温度4"));
    m_curveDisplayNames.insert(189, tr("从机故障代码1"));
    m_curveDisplayNames.insert(190, tr("从机故障代码2"));
    m_curveDisplayNames.insert(191, tr("从机故障代码3"));
    m_curveDisplayNames.insert(192, tr("从机故障代码4"));
    m_curveDisplayNames.insert(194, tr("从机运行模式"));
    m_curveDisplayNames.insert(195, tr("从机程序版本"));
    m_curveDisplayNames.insert(196, tr("从机冷凝温度1"));
    m_curveDisplayNames.insert(197, tr("从机冷凝温度2"));
    m_curveDisplayNames.insert(198, tr("从机冷凝温度3"));
    m_curveDisplayNames.insert(199, tr("从机冷凝温度4"));
    m_curveDisplayNames.insert(200, tr("从机内管1"));
    m_curveDisplayNames.insert(201, tr("从机内管2"));
    m_curveDisplayNames.insert(202, tr("从机内管3"));
    m_curveDisplayNames.insert(203, tr("从机内管4"));
    m_curveDisplayNames.insert(213, tr("从机蒸发温度1"));
    m_curveDisplayNames.insert(214, tr("从机蒸发温度2"));
    m_curveDisplayNames.insert(215, tr("从机蒸发温度3"));
    m_curveDisplayNames.insert(216, tr("从机蒸发温度4"));
    m_curveDisplayNames.insert(4003, tr("从机无霜环翅温差1"));
    m_curveDisplayNames.insert(4004, tr("从机无霜环翅温差2"));
    m_curveDisplayNames.insert(4005, tr("从机无霜环翅温差3"));
    m_curveDisplayNames.insert(4006, tr("从机无霜环翅温差4"));
    m_curveDisplayNames.insert(4012, tr("从机分配能力1"));
    m_curveDisplayNames.insert(4013, tr("从机分配能力2"));
    m_curveDisplayNames.insert(4014, tr("从机分配能力3"));
    m_curveDisplayNames.insert(4015, tr("从机分配能力4"));

    // BitToText 曲线：将“单个寄存器的某一位”拆成独立曲线，
    // curveKey 采用 (地址<<4)|bitPos 的编码方式，避免与普通地址冲突。
    // 例如：地址 3009 的 bit0 = 辅助加热开关；地址 3011 的 bit0~3 = 4 个低压开关。
    m_curveDisplayNames.insert((3009 << 4) | 0, tr("水泵1"));
    m_curveDisplayNames.insert((3009 << 4) | 1, tr("铺热"));
    m_curveDisplayNames.insert((3009 << 4) | 5, tr("四通阀1"));
    m_curveDisplayNames.insert((3009 << 4) | 10, tr("四通阀2"));
    m_curveDisplayNames.insert((3009 << 4) | 15, tr("四通阀3"));
    m_curveDisplayNames.insert((3010 << 4) | 4, tr("四通阀4"));
    m_curveDisplayNames.insert((3011 << 4) | 0, tr("低压开关1"));
    m_curveDisplayNames.insert((3011 << 4) | 1, tr("低压开关2"));
    m_curveDisplayNames.insert((3011 << 4) | 2, tr("低压开关3"));
    m_curveDisplayNames.insert((3011 << 4) | 3, tr("低压开关4"));
    m_curveDisplayNames.insert((3011 << 4) | 4, tr("联动开关"));
    m_curveDisplayNames.insert((3011 << 4) | 6, tr("水流开关"));
    m_curveDisplayNames.insert((3011 << 4) | 7, tr("高压开关1"));
    m_curveDisplayNames.insert((3011 << 4) | 8, tr("高压开关2"));
    m_curveDisplayNames.insert((3011 << 4) | 9, tr("高压开关3"));
    m_curveDisplayNames.insert((3011 << 4) | 10, tr("高压开关4"));
    //从机
    m_curveDisplayNames.insert((4009 << 4) | 0, tr("水泵2"));
    m_curveDisplayNames.insert((4009 << 4) | 1, tr("从机铺热"));
    m_curveDisplayNames.insert((4009 << 4) | 5, tr("从机四通阀1"));
    m_curveDisplayNames.insert((4009 << 4) | 10, tr("从机四通阀2"));
    m_curveDisplayNames.insert((4009 << 4) | 15, tr("从机四通阀3"));
    m_curveDisplayNames.insert((4010 << 4) | 4, tr("从机四通阀4"));
    m_curveDisplayNames.insert((4011 << 4) | 0, tr("从机低压开关1"));
    m_curveDisplayNames.insert((4011 << 4) | 1, tr("从机低压开关2"));
    m_curveDisplayNames.insert((4011 << 4) | 2, tr("从机低压开关3"));
    m_curveDisplayNames.insert((4011 << 4) | 3, tr("从机低压开关4"));
    m_curveDisplayNames.insert((4011 << 4) | 4, tr("从机联动开关"));
    m_curveDisplayNames.insert((4011 << 4) | 6, tr("从机水流开关"));
    m_curveDisplayNames.insert((4011 << 4) | 7, tr("从机高压开关1"));
    m_curveDisplayNames.insert((4011 << 4) | 8, tr("从机高压开关2"));
    m_curveDisplayNames.insert((4011 << 4) | 9, tr("从机高压开关3"));
    m_curveDisplayNames.insert((4011 << 4) | 10, tr("从机高压开关4"));
    m_curveDisplayNames.insert((5009 << 4) | 0, tr("水泵3"));


}

QList<CurveOption> Ofile2::getCurveOptions()
{
    QList<CurveOption> list;
    QSet<int> seenKeys; // 避免同一个 curveKey 被重复加入

    // 说明：
    // - 绝大部分地址：一个地址只映射到一个物理量（一个 QLineEdit），curveKey=地址本身；
    // - 对于 BitToText 类型（同一寄存器的不同 bit 表示不同开关）：
    //   为了能在曲线中分别选择这些开关，使用 “(地址 << 4) | bitPos” 作为 curveKey，
    //   即同一地址的不同位拥有不同的 curveKey。
    for (auto it = m_addressToRuleMap.begin(); it != m_addressToRuleMap.end(); ++it) {
        int addr = it.key();
        QList<ConversionRule> rules = m_addressToRuleMap.values(addr);
        if (rules.isEmpty()) continue;

        for (const ConversionRule &r : rules) {
            if (!r.lineEdit) continue;

            int curveKey = addr;
            // 对于 BitToText 规则，用 (addr<<4)|bitPos 区分同一寄存器的不同位
            if (r.ruleType == ConversionRule::Rule_BitToText &&
                r.bitPosition >= 0 && r.bitPosition <= 15) {
                curveKey = (addr << 4) | r.bitPosition;
            }

            if (seenKeys.contains(curveKey)) continue;
            seenKeys.insert(curveKey);

            CurveOption opt;
            opt.curveKey = curveKey;
            // 优先从 m_curveDisplayNames 取名称；若没有，则用“地址xx”或“地址xx_Bbit”作为兜底
            if (m_curveDisplayNames.contains(curveKey)) {
                opt.displayName = m_curveDisplayNames.value(curveKey);
            } else if (r.ruleType == ConversionRule::Rule_BitToText &&
                       r.bitPosition >= 0 && r.bitPosition <= 15) {
                opt.displayName = tr("地址%1_B%2").arg(addr).arg(r.bitPosition);
            } else {
                opt.displayName = m_curveDisplayNames.value(addr, tr("地址%1").arg(addr));
            }

            opt.unit = r.suffix.isEmpty() ? QString() : r.suffix;
            opt.lineEdit = r.lineEdit;
            list.append(opt);
        }
    }
    return list;
}

void Ofile2::updateFaultDisplay()
{
    // 清空当前内容
    ui->plainTextEdit->clear(); // 使用 clear() 替代复杂的 QTextCursor 操作，更简洁

    // 2. 遍历活动故障列表并写入
    if (m_activeFaults.isEmpty()) {
        ui->plainTextEdit->insertPlainText(tr(""));
    } else {
        // 遍历所有键值对 (地址, 故障消息)
        for (auto it = m_activeFaults.constBegin(); it != m_activeFaults.constEnd(); ++it) {
//            const QString &faultKey = it.key();
            const QString &message = it.value();

            // 提取地址和位信息用于显示 (例如：从 "2002_B10" 提取)
            // 简单地只显示故障消息也可以，但这里我们提供更多信息

            // 格式化输出到 PlainTextEdit
            // 使用 HTML 设置红色字体
//            const QString faultLine = tr("<font color=\"red\">%1 (地址: %2)</font>\n").arg(message).arg(faultKey);
            const QString faultLine = tr("<font color=\"red\">%1</font>\n").arg(message);
            ui->plainTextEdit->appendHtml(faultLine);
        }
    }

    // 将滚动条滚动到底部
    ui->plainTextEdit->ensureCursorVisible();
}

void Ofile2::on_button_Connect_clicked()
{
    if (!modbusDevice)
        return;
    // --- 【关键行】在连接前初始化请求队列 ---
    initializeModbusBlocks();
    if (m_requestQueue.isEmpty())
    {
        QMessageBox::warning(this, tr("警告"), tr("没有配置任何Modbus地址，无法启动读取。"));
        return;
    }
    if(ui->button_Connect->text()==QString("连接"))
    {
        // 串口设备
        modbusDevice->setConnectionParameter(QModbusDevice::SerialPortNameParameter,ui->comboBox_Serial_Port_Selection->currentText());
        modbusDevice->setConnectionParameter(QModbusDevice::SerialBaudRateParameter,ui->comboBox_Baud_rate->currentText().toInt());
        //设置奇偶校验
        modbusDevice->setConnectionParameter(QModbusDevice::SerialParityParameter,QSerialPort::NoParity);
        //设置数据位数
        modbusDevice->setConnectionParameter(QModbusDevice::SerialDataBitsParameter,QSerialPort::Data8);
        //设置停止位
        modbusDevice->setConnectionParameter(QModbusDevice::SerialStopBitsParameter,QSerialPort::OneStop);


        // 在 modbusDevice->connectDevice() 之前设置
        modbusDevice->setTimeout(1000);         // 设置单次超时为 2000ms
        modbusDevice->setNumberOfRetries(0);    // 失败重试 3 次


        m_isChainPolling = true;
        m_waitingForReply = false;
        ReadRequest(); // 启动第一发


        // 设置控件可否使用
        if (modbusDevice->connectDevice())
        {
            //开启自动读取
            connect(pollTimer,&QTimer::timeout, this, &Ofile2::ReadRequest);
            pollTimer->setInterval(100);
            pollTimer->start();
            ui->button_Connect->setText(tr("断开"));
        }
        else    //打开失败提示
        {
            QMessageBox::information(this,tr("错误"),tr("连接从站失败！"),QMessageBox::Ok);
        }
    }
    else
    {
        modbusDevice->disconnectDevice();
        ui->button_Connect->setText(tr("连接"));
    }

}

void Ofile2::initializeModbusBlocks()
{
    // 清空现有队列
    m_requestQueue.clear();

    // 1. 获取所有需要读取的地址
        QSet<int> allAddressesSet; // 使用 QSet 自动去重

        // a) 添加 UI 规则地址
        for (int address : m_addressToRuleMap.keys()) {
            allAddressesSet.insert(address);
        }

        // b) 添加纯故障监控地址 <-- 【关键修正】
        for (int address : m_addressToMonitorMap.keys()) {
            allAddressesSet.insert(address);
        }

        // 转换为 QList 并排序
            QList<int> allAddresses = allAddressesSet.values();
            if (allAddresses.isEmpty())
                return;


    // 2. 排序，这是分块的基础
    std::sort(allAddresses.begin(), allAddresses.end());

    int currentStart = allAddresses.first();
    quint16 currentCount = 1;

    // 3. 遍历并合并连续块
    for (int i = 1; i < allAddresses.size(); ++i) {
        int previousAddress = allAddresses.at(i - 1);
        int currentAddress = allAddresses.at(i);

        if (currentAddress == previousAddress + 1) {
            // 地址连续，增加块计数
            currentCount++;
        } else {
            // 地址不连续，结束当前块，保存到队列
            m_requestQueue.append({currentStart, currentCount});

            // 开启新块
            currentStart = currentAddress;
            currentCount = 1;
        }
    }

    // 循环结束后，保存最后一个块
    m_requestQueue.append({currentStart, currentCount});

    // 重置当前索引
    m_currentBlockIndex = 0;
}

void Ofile2::setupControlSystem() {
    m_controlConfigs.clear();

    // 1. 开关机控制 (Coils 类型, 地址 0)
    m_controlConfigs.append({ui->comboBox_Operation, QModbusDataUnit::Coils, 0, tr("关机"), tr("开机")});

    // 2. 模式控制 (新增：HoldingRegisters 类型, 地址 3)
    // 假设您的模式下拉框对象名是 ui->comboBox_Mode
//    m_controlConfigs.append({ui->comboBox_4, QModbusDataUnit::HoldingRegisters, 0, tr("制热"), tr("制冷"), tr("制冷")});

    // 3. 进出水控制 (Coils 类型, 地址 6)
//   m_controlConfigs.append({ui->comboBox_10, QModbusDataUnit::Coils, 6, tr("进水"), tr("出水")});

    // 自动初始化所有下拉框的文字选项
    for (const auto &config : m_controlConfigs) {
        config.comboBox->clear();
        config.comboBox->addItems({config.textFor0, config.textFor1});
    }
}

void Ofile2::setupHoldingControls() {
    m_holdingConfigs.clear();

    // --- 增量示例 1：模式口 (5个选项，地址3，偏移1) ---
    m_holdingConfigs.append({ui->comboBox_Mode, 0,
        {tr("制冷"), tr("制热"), tr("热水"), tr("制冷+热水"), tr("制热+热水")}, 1});

    m_holdingConfigs.append({ui->comboBox_Silent_Mode, 3,
        {tr("开"), tr("关")}, 1});


//    // --- 增量示例 2：参数口 (超过20个选项，地址5，偏移0) ---
//    QStringList longOptions;
//    for(int i = 0; i < 25; ++i) longOptions << tr("参数 %1").arg(i);
//    m_holdingConfigs.append({ui->comboBox_Other, 5, longOptions, 0});

//    // --- 增量示例 3：简单口 (2个选项，地址10，偏移0) ---
//    m_holdingConfigs.append({ui->comboBox_Simple, 10, {tr("关闭"), tr("开启")}, 0});
    // --- 新增：数字选择口 (比如地址 10, 选择范围 1-60) ---
//        HoldingConfig numericConfig;
//        numericConfig.comboBox = ui->comboBox_5; // 替换为您 UI 上的实际对象名
//        numericConfig.address = 2;                     // 目标寄存器地址
    //        numericConfig.offset = 20;                       // 索引0对应数字1，所以偏移量为1

    //        QStringList numList;
    //        for(int i =20 ; i <= 55; ++i) {
    //            numList << QString::number(i);              // 生成 "1", "2", ..., "60"
    //        }
    //        numericConfig.options = numList;
    //        m_holdingConfigs.append(numericConfig);

//    QStringList range55;//制热出水
//    for(int i = 20; i <= 55; ++i) range55 << QString::number(i); // 自动生成1-60
//    m_holdingConfigs.append({ui->comboBox_6, 5, range55, 20});

    QStringList range50;//制热进水
    for(int i = 25; i <= 50; ++i) range50 << QString::number(i); // 自动生成1-60
    m_holdingConfigs.append({ui->comboBox_Heating_Inlet_Water_Setting, 2, range50, 25});

    QStringList range25;//制冷进水
    for(int i = 12; i <= 25; ++i) range25 << QString::number(i); // 自动生成1-60
    m_holdingConfigs.append({ui->comboBox_Chilled_Water_Inlet_Setting, 1, range25, 3});

//    QStringList range30;//制冷出水
//    for(int i = 3; i <= 25; ++i) range30 << QString::number(i); // 自动生成1-60
//    m_holdingConfigs.append({ui->comboBox_11, 4, range30, 3});


    // 统一初始化 UI（此部分代码固定，不随口数增加）
    for (const auto &config : m_holdingConfigs) {
        config.comboBox->clear();
        config.comboBox->addItems(config.options);
    }
    qDebug() << "调用到了"; // 加上这一行调试
}


void Ofile2::on_pushButton_Application_clicked()
{
    if (!modbusDevice || modbusDevice->state() != QModbusDevice::ConnectedState) {
        QMessageBox::warning(this, tr("提示"), tr("请先连接设备"));
        return;
    }

    if (QMessageBox::question(this, tr("确认"), tr("是否应用当前所有配置更改？")) != QMessageBox::Yes) return;

    // 写入 HoldingRegisters (包括模式口和新增的数字口)
    for (const auto &config : m_holdingConfigs) {
        int index = config.comboBox->currentIndex();
        if (index < 0) continue;

        // 核心逻辑：写入值 = 索引 + 偏移量
        // 对于 1-60，索引 0 + offset 1 = 写入值 1
        int writeValue = index + config.offset;

        QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, config.address, 1);
        writeUnit.setValue(0, writeValue);

        if (auto *reply = modbusDevice->sendWriteRequest(writeUnit, 1)) {
            connect(reply, &QModbusReply::finished, this, [reply, config, this]() {
                if (reply->error() != QModbusDevice::NoError) {
                    statusBar()->showMessage(tr("地址 %1 写入失败").arg(config.address), 5000);
                }
                reply->deleteLater();
            });
        }
    }
    // --- 写入线圈类 (开关机、进出水) ---
    for (const auto &config : m_controlConfigs) {
        QModbusDataUnit writeUnit(config.type, config.address, 1);
        writeUnit.setValue(0, config.comboBox->currentIndex());
        modbusDevice->sendWriteRequest(writeUnit, 1);
    }

    statusBar()->showMessage(tr("指令已批量发送"), 3000);
}


void Ofile2::onModbusStateChanged(QModbusDevice::State state)
{
    if (state == QModbusDevice::ConnectedState) {
        // 若正在回放，连接成功后先停止回放，避免两套数据源打架
        stopPlayback();
        // 条件二：如果“连接时清零口”已勾选，或者之前没有在“断开继续计时”状态
        if (ui->checkBox_Reset_when_connecting->isChecked()) {
            m_totalSeconds = 0;
        }
        else if (!ui->checkBox_Continue_counting_time_when_disconnected->isChecked()) {
            // 如果没勾选“断开继续”，重连时通常也重新开始
            m_totalSeconds = 0;
        }

        if (!m_durationTimer->isActive()) {
            m_durationTimer->start(1000); // 开启秒计
        }

        if (!m_curvesDlg) {
            m_curvesDlg = new CurvesDialog(this);
            m_curvesDlg->setCurveOptions(getCurveOptions());
        } else {
            m_curvesDlg->setCurveOptions(getCurveOptions());
        }
        if (!m_curveRecordTimer->isActive()) {
            m_curveRecordTimer->start(1000);
        }
        // 串口连接成功：自动开始 SQLite 存储
        startSqliteRecording();
    } else {
        if (m_curveRecordTimer && m_curveRecordTimer->isActive()) {
            m_curveRecordTimer->stop();
        }
        // 断开：停止并关闭数据库（确保落盘）
        stopSqliteRecording();
    }
}

void Ofile2::onCurveRecordTimer()
{
    if (m_curvesDlg) {
        m_curvesDlg->recordCurrentValuesFromOptions();
    }

    // 同步写入 SQLite（所有可选通道都记录，避免后选曲线无历史）
    if (m_sqliteRecorder && m_sqliteRecorder->isActive()) {
        const QList<CurveOption> opts = getCurveOptions();
        std::vector<std::pair<int, double>> sampleData;
        sampleData.reserve(static_cast<size_t>(opts.size()));
        for (const CurveOption &opt : opts) {
            if (!opt.lineEdit) continue;

            double v = 0.0;
            // 先看控件上是否有曲线用原始数值（例如状态 0/1/2...）
            QVariant raw = opt.lineEdit->property("curveNumericValue");
            if (raw.isValid()) {
                bool ok = false;
                v = raw.toDouble(&ok);
                if (!ok) continue;
            } else {
                if (!tryParseDouble(opt.lineEdit->text(), v))
                    continue;
            }

            sampleData.push_back({opt.curveKey, v});
        }
        m_sqliteRecorder->recordSample(QDateTime::currentDateTime(), sampleData);
    }
}

void Ofile2::updateConnectionTimer()
{
    bool isConnected = (modbusDevice && modbusDevice->state() == QModbusDevice::ConnectedState);
    bool continueRunning = ui->checkBox_Continue_counting_time_when_disconnected->isChecked(); // 断开时继续计时口

    // 逻辑：连接中 或者 (断开但勾选了继续计时)
    if (isConnected || continueRunning) {

        if (isConnected || (continueRunning && m_totalSeconds > 0)) {
             m_totalSeconds++;
        }
    }
    // 功能二：连接时清零口 (checkBox)
    if (ui->checkBox_Reset_when_connecting->isChecked()) {
        m_totalSeconds = 0;
        ui->checkBox_Reset_when_connecting->setChecked(false); // 清零后自动取消勾选，或者保持勾选持续清零
    }

    // 格式化输出：0时0分0秒
    int h = m_totalSeconds / 3600;
    int m = (m_totalSeconds % 3600) / 60;
    int s = m_totalSeconds % 60;
    ui->lineEdit_2->setText(QString("%1时%2分%3秒").arg(h).arg(m).arg(s));
}
void Ofile2::on_checkBox_Reset_when_connecting_stateChanged(int arg1)
{
    // 如果勾选 (Qt::Checked = 2)
    if (arg1 == Qt::Checked) {
        m_totalSeconds = 0;
        ui->lineEdit_2->setText("0时0分0秒");
    }
}

void Ofile2::on_ValveCommand_clicked()
{
    if (!m_valveCommandDlg) {
            m_valveCommandDlg = new ValveCommand(modbusDevice, this);
        }

        // 关键：在弹出模态窗口前，必须停止主界面的 pollTimer 轮询
        // 否则主界面的读取会和对话框的“写位”请求在串口线上撞车
        if(pollTimer->isActive()) pollTimer->stop();

        m_valveCommandDlg->readAllValves();
        m_valveCommandDlg->exec();

        // 窗口关闭后，重新开启轮询
        if(!pollTimer->isActive()) pollTimer->start(2000);
}

void Ofile2::on_AdjustCompressor_clicked()
{
    // 如果窗口还没创建，则创建它
    if (!m_adjustCompressorDlg) {
        m_adjustCompressorDlg = new AdjustCompressor(modbusDevice, this);
    }

    // 每次弹出前，手动触发一次读取请求
    // 这样如果连接着，它会更新；如果没连接，它会保留上次的文本
    m_adjustCompressorDlg->readAllValves();

    m_adjustCompressorDlg->exec(); // 模态显示
}
// 封装的具体实现
void Ofile2::setupLayouts()
{
    // A. 实现随窗口大小自动放大缩小
    // 如果 centralwidget 还没有布局，则创建一个垂直布局
    if (!ui->centralwidget->layout()) {
        QVBoxLayout *mainLayout = new QVBoxLayout(ui->centralwidget);
        mainLayout->setContentsMargins(0, 0, 0, 0); // 消除边距，使滚动条贴边
        mainLayout->setSpacing(0);
        mainLayout->addWidget(ui->sArea1); // 将 ScrollArea 放入布局，使其随动
    }

    // B. 设置触发滚动条的阈值 (500x500)
    // widgetResizable 必须为 true，这样内部组件在大窗口时会自动拉伸
    ui->sArea1->setWidgetResizable(true);

    // 设置内部容器的最小尺寸。当 ScrollArea 尺寸小于此值时，滚动条自动出现
    ui->scrollAreaWidgetContents->setMinimumSize(1810, 910);

    // C. 确保内部 TabWidget 也能随动 (可选但强烈建议)
    // 如果你的 tabWidget 还在用绝对坐标，建议给内部容器也加个布局
    if (!ui->scrollAreaWidgetContents->layout()) {
        QVBoxLayout *contentLayout = new QVBoxLayout(ui->scrollAreaWidgetContents);
        contentLayout->addWidget(ui->widget); // 让 tabWidget 填满滚动区域内部
    }
}


void Ofile2::on_pushButton_parameters_clicked()
{
    // 1. 停止主界面的轮询，防止总线竞争
    if (m_isChainPolling) {
        m_isChainPolling = false;
    }

    // 2. 创建并显示模态对话框
    pushButton_parameters configDlg(modbusDevice, this);
    configDlg.setWindowTitle(tr("从机配置参数设置"));

    // exec() 会阻塞主循环，直到窗口关闭
    configDlg.exec();

    // 3. 窗口关闭后，恢复主界面轮询
    m_isChainPolling = true;
    m_waitingForReply = false;
    ReadRequest();
}

void Ofile2::on_pushButton_Curves_clicked()
{
    if (!m_curvesDlg) {
        m_curvesDlg = new CurvesDialog(this);
    }
    m_curvesDlg->setCurveOptions(getCurveOptions());
    m_curvesDlg->setModal(false);
    m_curvesDlg->show();
    m_curvesDlg->raise();
    m_curvesDlg->activateWindow();
}

void Ofile2::on_actionShowCurves_triggered()
{
    if (!m_curvesDlg) {
        m_curvesDlg = new CurvesDialog(this);
    }
    m_curvesDlg->setCurveOptions(getCurveOptions());
    m_curvesDlg->setModal(false);
    m_curvesDlg->show();
    m_curvesDlg->raise();
    m_curvesDlg->activateWindow();
}

void Ofile2::on_actionStartPlayback_triggered()
{
    // 【开始回放】主入口。
    // 设计意图：
    // - 回放只能在离线状态下进行（避免在线采集与回放混在一起）；
    // - 支持两种模式：
    //   1) 录像式：按时间间隔从数据库中一点点播放出来；
    //   2) 直接加载全部：一次性加载整段历史，用户通过滑动条在时间轴上任意跳转。
    // - 回放时，曲线窗口作为“显示终端”，主界面则展示“回放：进度条 + 总时长 + 当前位置时间”。
    // 未连接时回放；如已连接则提示先断开
    if (modbusDevice && modbusDevice->state() == QModbusDevice::ConnectedState) {
        QMessageBox::information(this, tr("开始回放"), tr("请先断开串口连接，再进行离线回放。"));
        return;
    }

    PlaybackSettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString dbPath = dlg.databaseFilePath();
    int intervalMs = dlg.playbackIntervalMs();
    bool loadAll = dlg.loadAllAtOnce();

    if (dbPath.isEmpty() || !QFileInfo::exists(dbPath)) {
        QMessageBox::warning(this, tr("开始回放"), tr("请选择有效的 .sqlite 数据库文件。"));
        return;
    }

    // 为了避免录制/回放之间互相干扰，开始前先确保两者都处于停止状态
    stopSqliteRecording();
    stopPlayback();

    // 打开回放数据库（单独使用一个连接，避免与录制连接混用）
    m_playbackConnName = QStringLiteral("playback_%1").arg(QDateTime::currentMSecsSinceEpoch());
    m_playbackFilePath = dbPath;
    m_playbackDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_playbackConnName);
    m_playbackDb.setDatabaseName(m_playbackFilePath);
    if (!m_playbackDb.open()) {
        QMessageBox::warning(this, tr("开始回放"), tr("打开数据库失败：%1").arg(m_playbackDb.lastError().text()));
        stopPlayback();
        return;
    }

    // 打开曲线窗口作为回放显示
    if (!m_curvesDlg) {
        m_curvesDlg = new CurvesDialog(this);
    }
    m_curvesDlg->setCurveOptions(getCurveOptions());
    m_curvesDlg->show();

    // 显示“回放：”前缀
    if (ui->label_PlaybackPrefix)
        ui->label_PlaybackPrefix->show();

    if (loadAll) {
        // 一次性加载全部样本数据，并构建时间轴，供滑动条使用
        QSqlQuery q(m_playbackDb);
        if (!q.exec(QStringLiteral("SELECT ts_ms, curve_key, value FROM samples ORDER BY ts_ms ASC, curve_key ASC;"))) {
            QMessageBox::warning(this, tr("开始回放"), tr("读取回放数据失败：%1").arg(q.lastError().text()));
            stopPlayback();
            return;
        }

        const QList<CurveOption> opts = getCurveOptions();
        QMap<int, QLineEdit*> keyToEdit;
        for (const CurveOption &opt : opts) {
            if (opt.lineEdit)
                keyToEdit.insert(opt.curveKey, opt.lineEdit);
        }

        m_playbackTsMs.clear();
        qint64 currentTs = -1;
        std::vector<std::pair<int, double>> sampleData;

        while (q.next()) {
            qint64 ts = q.value(0).toLongLong();
            int key   = q.value(1).toInt();
            double v  = q.value(2).toDouble();

            // 若遇到新的时间戳，先把上一帧写入曲线，并记录时间轴
            if (currentTs != -1 && ts != currentTs) {
                m_curvesDlg->addSample(sampleData, QDateTime::fromMSecsSinceEpoch(currentTs));
                m_playbackTsMs.push_back(currentTs);
                sampleData.clear();
            }
            currentTs = ts;
            sampleData.push_back({key, v});

            if (keyToEdit.contains(key)) {
                keyToEdit.value(key)->setText(QString::number(v, 'f', 1));
            }
        }

        if (currentTs != -1 && !sampleData.empty()) {
            m_curvesDlg->addSample(sampleData, QDateTime::fromMSecsSinceEpoch(currentTs));
            m_playbackTsMs.push_back(currentTs);
        }

        m_playbackTsIndex = 0;

        // 一次性加载模式：显示总时长和起始时间；滑动条可滑动但不自动播放（不启用定时器）
        if (!m_playbackTsMs.isEmpty()) {
            qint64 first = m_playbackTsMs.first();
            qint64 last  = m_playbackTsMs.last();
            qint64 totalSec = (last - first) / 1000;

            if (ui->slider_Playback) {
                ui->slider_Playback->setMinimum(0);
                ui->slider_Playback->setMaximum(m_playbackTsMs.size() - 1);
                ui->slider_Playback->setValue(0);
                ui->slider_Playback->setEnabled(true);   // 允许拖动
                ui->slider_Playback->show();
            }
            if (ui->label_PlaybackTotal) {
                ui->label_PlaybackTotal->setText(tr("总计：%1 秒").arg(totalSec));
                ui->label_PlaybackTotal->show();
            }
            if (ui->label_PlaybackCurrent) {
                QDateTime dt = QDateTime::fromMSecsSinceEpoch(first);
                ui->label_PlaybackCurrent->setText(
                    tr("当前位置：%1").arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
                ui->label_PlaybackCurrent->show();
            }
        }
    } else {
        // 保留原有“录像式”按时间间隔播放逻辑
        QSqlQuery q(m_playbackDb);
        if (!q.exec(QStringLiteral("SELECT DISTINCT ts_ms FROM samples ORDER BY ts_ms ASC;"))) {
            QMessageBox::warning(this, tr("开始回放"), tr("读取回放数据失败：%1").arg(q.lastError().text()));
            stopPlayback();
            return;
        }
        while (q.next()) {
            m_playbackTsMs.push_back(q.value(0).toLongLong());
        }
        if (m_playbackTsMs.isEmpty()) {
            QMessageBox::information(this, tr("开始回放"), tr("数据库中没有可回放的数据。"));
            stopPlayback();
            return;
        }

        m_playbackTsIndex = 0;
        m_playbackTimer->start(qMax(1, intervalMs));

        // 录像式播放：启用可滑动进度条和播放/暂停按钮
        if (ui->slider_Playback) {
            ui->slider_Playback->setMinimum(0);
            ui->slider_Playback->setMaximum(m_playbackTsMs.size() - 1);
            ui->slider_Playback->setValue(0);
            ui->slider_Playback->setEnabled(true);
            ui->slider_Playback->show();
        }
        if (!m_playbackTsMs.isEmpty()) {
            qint64 first = m_playbackTsMs.first();
            qint64 last  = m_playbackTsMs.last();
            qint64 totalSec = (last - first) / 1000;
            if (ui->label_PlaybackTotal)
                ui->label_PlaybackTotal->setText(tr("总计：%1 秒").arg(totalSec));
            if (ui->label_PlaybackCurrent) {
                QDateTime dt = QDateTime::fromMSecsSinceEpoch(first);
                ui->label_PlaybackCurrent->setText(
                    tr("当前位置：%1").arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
            }
            if (ui->label_PlaybackTotal) ui->label_PlaybackTotal->show();
            if (ui->label_PlaybackCurrent) ui->label_PlaybackCurrent->show();
        }
        m_playbackPaused = false;
    }
}

void Ofile2::onPlaybackTimerTick()
{
    // 录像式回放模式下的“每一帧”更新逻辑：
    // - 按 m_playbackTsIndex 从 m_playbackTsMs 取出当前时间戳；
    // - 从 samples 表中读出该时刻所有曲线的值，写回 UI 和曲线；
    // - 同步更新主界面上的回放滑动条和“当前位置时间”；
    // - 索引自增，直到播放完整个时间轴。
    if (!m_playbackDb.isValid() || !m_playbackDb.isOpen()) {
        stopPlayback();
        return;
    }
    if (!m_curvesDlg) {
        stopPlayback();
        return;
    }
    if (m_playbackTsIndex < 0 || m_playbackTsIndex >= m_playbackTsMs.size()) {
        // 播放结束：只停止定时器，保留滑动条和时间信息，方便回看
        if (m_playbackTimer && m_playbackTimer->isActive())
            m_playbackTimer->stop();
        m_playbackPaused = true;
        return;
    }

    const qint64 tsMs = m_playbackTsMs.at(m_playbackTsIndex);
    QSqlQuery q(m_playbackDb);
    q.prepare(QStringLiteral("SELECT curve_key, value FROM samples WHERE ts_ms = ?;"));
    q.addBindValue(tsMs);
    if (!q.exec()) {
        stopPlayback();
        return;
    }

    const QList<CurveOption> opts = getCurveOptions();
    QMap<int, QLineEdit*> keyToEdit;
    for (const CurveOption &opt : opts) {
        if (opt.lineEdit)
            keyToEdit.insert(opt.curveKey, opt.lineEdit);
    }

    std::vector<std::pair<int, double>> sampleData;
    while (q.next()) {
        int key = q.value(0).toInt();
        double v = q.value(1).toDouble();
        sampleData.push_back({key, v});
        if (keyToEdit.contains(key)) {
            keyToEdit.value(key)->setText(QString::number(v, 'f', 1));
        }
    }

    // 灌入曲线缓冲区（使用数据库时间戳）
    m_curvesDlg->addSample(sampleData, QDateTime::fromMSecsSinceEpoch(tsMs));

    // 更新数据页上的回放控制条
    if (ui->slider_Playback) {
        ui->slider_Playback->blockSignals(true);
        ui->slider_Playback->setValue(m_playbackTsIndex);
        ui->slider_Playback->blockSignals(false);
    }
    if (ui->label_PlaybackCurrent) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(tsMs);
        ui->label_PlaybackCurrent->setText(
            tr("当前位置：%1").arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }

    m_playbackTsIndex += 1;
}

void Ofile2::on_slider_Playback_valueChanged(int value)
{
    // 回放滑动条被拖动时的逻辑：
    // - 允许用户在时间轴上任意跳转到某一帧；
    // - 逻辑与 onPlaybackTimerTick 类似，只是不会自增索引、也不会自动继续播放。
    if (m_playbackTsMs.isEmpty())
        return;
    if (value < 0 || value >= m_playbackTsMs.size())
        return;

    m_playbackTsIndex = value;

    if (!m_playbackDb.isValid() || !m_playbackDb.isOpen())
        return;
    if (!m_curvesDlg)
        return;

    const qint64 tsMs = m_playbackTsMs.at(m_playbackTsIndex);
    QSqlQuery q(m_playbackDb);
    q.prepare(QStringLiteral("SELECT curve_key, value FROM samples WHERE ts_ms = ?;"));
    q.addBindValue(tsMs);
    if (!q.exec())
        return;

    const QList<CurveOption> opts = getCurveOptions();
    QMap<int, QLineEdit*> keyToEdit;
    for (const CurveOption &opt : opts) {
        if (opt.lineEdit)
            keyToEdit.insert(opt.curveKey, opt.lineEdit);
    }

    std::vector<std::pair<int, double>> sampleData;
    while (q.next()) {
        int key = q.value(0).toInt();
        double v = q.value(1).toDouble();
        sampleData.push_back({key, v});
        if (keyToEdit.contains(key)) {
            keyToEdit.value(key)->setText(QString::number(v, 'f', 1));
        }
    }

    // 用数据库时间戳刷新曲线
    m_curvesDlg->addSample(sampleData, QDateTime::fromMSecsSinceEpoch(tsMs));

    if (ui->label_PlaybackCurrent) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(tsMs);
        ui->label_PlaybackCurrent->setText(
            tr("当前位置：%1").arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    }
}




