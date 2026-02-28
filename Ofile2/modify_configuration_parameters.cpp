#include "modify_configuration_parameters.h"
#include "ui_modify_configuration_parameters.h"
#include <QModbusReply>
#include <QDebug>


Modify_configuration_parameters::Modify_configuration_parameters(QModbusClient *modbusClient, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Modify_configuration_parameters),
    m_modbusClient(modbusClient)
{
    ui->setupUi(this);
    this->setFixedSize(631,576);
    setupMapping();
    // 2. 如果已经连接，尝试自动读取一次实际值
    if (m_modbusClient && m_modbusClient->state() == QModbusDevice::ConnectedState) {
        on_btnRead_clicked();
    }

}

Modify_configuration_parameters::~Modify_configuration_parameters()
{
    delete ui;
}


void Modify_configuration_parameters::setupMapping()
{
    m_configMappings.clear();
    // 示例 A: 温度输入框 (SpinBox)，地址 200，范围 -20 到 100，默认 25
    //除霜检测A点
    //    addMapping(1178, ui->spinBox_Repair_Button_Defrost_Curve_Point_A, 1.0, true, 60.0, 20.0, 150.0);

    addMapping(20, ui->spinBox_Repair_Button_Defrost_Curve_Point_A, 1.0, false, -6.0, -10.0, 200.0);
    //除霜检测B点
    addMapping(21, ui->spinBox_Repair_Button_Defrost_Curve_Point_B, 1.0, false, -26.0, -30.0, -15.0);
    //除霜结束温度
    addMapping(22, ui->spinBox_Repair_Button_Defrost_End_Temperature, 1.0, false, 15.0, 5.0, 50.0);
    //除霜间隔时间
    addMapping(23, ui->spinBox_Repair_Button_Defrost_Interval, 1.0, false, 45.0, 25.0, 90.0);
    //除霜运行时间
    addMapping(24, ui->spinBox_Repair_Button_Defrost_operating_time, 1.0, false, 10.0, 5.0, 15.0);
    //能力计算周期
    addMapping(25, ui->spinBox_Repair_Button_Capacity_Calculation_Cycle, 1.0, false, 40.0, 10.0, 90.0);
    //制冷防冻设置温度
    addMapping(26, ui->spinBox_Repair_Button_Refrigeration_and_antifreeze_temperature, 1.0, false, 5.0, 2.0, 5.0);
    //制热防冻设置温度
    addMapping(27, ui->spinBox_Repair_Button_Winter_anti_freeze_temperature, 1.0, false, 5.0, 2.0, 5.0);
    //制热启动温差
    addMapping(29, ui->lineEdit_Repair_Button_Heating_Start_Temperature_Difference, 0.1, false, 3.0, 0.1, 5.0);
    //制冷启动温差
    addMapping(28, ui->lineEdit_Repair_Button_Refrigeration_startup_temperature_difference, 0.1, false, 3.0, 0.1, 5.0);
    //水泵预运行时间
    addMapping(30, ui->spinBox_Repair_Button_Water_Pump_Pre_Operation_Time, 1.0, false, 100.0, 60.0, 200.0);
    //制冷出水设置
    addMapping(4, ui->spinBox_Repair_Button_Chilled_Water_Output_Settings, 1.0, false, 12.0, 6.0, 20.0);
    //制热出水设置
    addMapping(5, ui->spinBox_Repair_Button_Heating_Water_Outlet_Settings, 1.0, false, 50.0, 30.0, 55.0);
    //制热停机温差
    addMapping(33, ui->lineEdit_Repair_Button_Heating_shutdown_temperature_difference, 0.1, false, 1.2, 0.1, 5.0);
    //制冷停机温差
    addMapping(34, ui->lineEdit_Repair_Button_Cooling_shutdown_temperature_difference, 0.1, false, 1.2, 0.1, 5.0);


    // 示例 1: 整数框 (LineEdit)
    // 地址 20, 因子 1.0, 默认值 10, 范围 0~100
    //        addMapping(20, ui->lineEdit_Repair_Button_EXV1, 1.0, false, 350.0, 20.0, 480.0);

    // 示例 2: 一位小数框 (LineEdit)
    // 地址 21, 因子 0.1, 默认值 25.5, 范围 -10.0~50.0
    // 注意：这里的默认值和范围直接传缩放后的数值即可
    //        addMapping(21, ui->lineEdit_Decimal, 0.1, true, 25.5, -10.0, 50.0);
    // 示例 B: 压力显示框 (LineEdit)，地址 201，支持一位小数，范围 0.0 到 50.0
    //        addMapping(34, ui->lineEdit_Repair_Button_Heating_shutdown_temperature_difference, 0.1, false, 0.1, 0.0, 50.0);

    // 示例 C: 频率设置 (SpinBox)，范围 0 到 60Hz
    //        addMapping(202, ui->spinBox_Freq, 1.0, true, 50.0, 0.0, 60.0);

    // 示例 D: 模式选择 (ComboBox) - ComboBox 通常不需要手动设上下限，由选项决定
    // 注意参数顺序：地址, 控件, 缩放, 符号, 默认值, 最小值, 最大值, 选项包
    QMap<int, QString> Fan1;
    Fan1.insert(0, "停止");
    Fan1.insert(1, "低速");
    Fan1.insert(2, "中速");
    Fan1.insert(3, "高速");
    addMapping(200, ui->comboBox_Repair_Button_Fan1, 1.0, false, 0.0, 0.0, 1.0, Fan1);

    QMap<int, QString> Fan2;
    Fan2.insert(0, "停止");
    Fan2.insert(1, "低速");
    Fan2.insert(2, "中速");
    Fan2.insert(3, "高速");
    addMapping(200, ui->comboBox_Repair_Button_Fan2, 1.0, false, 0.0, 0.0, 1.0, Fan2);

    //EXV1
    addMapping(33, ui->lineEdit_Repair_Button_EXV1, 1.0, false, 350.0, 20.0, 480.0);
    //EXV2
    addMapping(34, ui->lineEdit_Repair_Button_EXV2, 1.0, false, 350.0, 0.0, 480.0);
    //EXV3
    addMapping(33, ui->lineEdit_Repair_Button_EXV3, 1.0, false, 60.0, 0.0, 480.0);
    //EXV4
    addMapping(34, ui->lineEdit_Repair_Button_EXV4, 1.0, false, 60.0, 0.0, 480.0);

    QMap<int, QString> EV1;
    EV1.insert(0, "关");
    EV1.insert(1, "开");
    addMapping(200, ui->comboBox_Repair_Button_EV1, 1.0, false, 0.0, 0.0, 1.0, EV1);

    QMap<int, QString> EV2;
    EV2.insert(0, "关");
    EV2.insert(1, "开");
    addMapping(200, ui->comboBox_Repair_Button_EV2, 1.0, false, 0.0, 0.0, 1.0, EV2);

    //副EXV1
    addMapping(33, ui->lineEdit_Repair_Button_Deputy_EXV1, 1.0, false, 350.0, 20.0, 480.0);
    //副EXV2
    addMapping(34, ui->lineEdit_Repair_Button_Deputy_EXV2, 1.0, false, 350.0, 0.0, 480.0);
    //副EXV3
    addMapping(33, ui->lineEdit_Repair_Button_Deputy_EXV3, 1.0, false, 60.0, 0.0, 480.0);
    //副EXV4
    addMapping(34, ui->lineEdit_Repair_Button_Deputy_EXV4, 1.0, false, 60.0, 0.0, 480.0);

    //压缩机
    QMap<int, QString> YSJ1;
    YSJ1.insert(0, "关");
    YSJ1.insert(1, "开");
    addMapping(200, ui->comboBox_Repair_Button_Compressor_1, 1.0, false, 0.0, 0.0, 1.0, YSJ1);

    QMap<int, QString> YSJ2;
    YSJ2.insert(0, "关");
    YSJ2.insert(1, "开");
    addMapping(200, ui->comboBox_Repair_Button_Compressor_2, 1.0, false, 0.0, 0.0, 1.0, YSJ2);

    QMap<int, QString> YSJ3;
    YSJ3.insert(0, "关");
    YSJ3.insert(1, "开");
    addMapping(200, ui->comboBox_Repair_Button_Compressor_3, 1.0, false, 0.0, 0.0, 1.0, YSJ3);

//    QMap<int, QString> YSJ4;
//    YSJ4.insert(0, "关");
//    YSJ4.insert(1, "开");
//    addMapping(200, ui->comboBox_Repair_Button_Compressor_4, 1.0, false, 0.0, 0.0, 1.0, YSJ4);

    //四通阀
    QMap<int, QString> STFa;
    STFa.insert(0, "关");
    STFa.insert(1, "开");
    addMapping(200, ui->comboBox_Repair_Button_Four_way_valve_A, 1.0, false, 0.0, 0.0, 1.0, STFa);

    QMap<int, QString> STFb;
    STFb.insert(0, "关");
    STFb.insert(1, "开");
    addMapping(200, ui->comboBox_Repair_Button_Four_way_valve_B, 1.0, false, 0.0, 0.0, 1.0, STFb);
    //        // 原有的 SpinBox 依然兼容
    //        addMapping(200, ui->spinBox_Temp, 1.0, true, 25.0);
    //        addMapping(33, ui->lineEdit_Repair_Button_Heating_shutdown_temperature_difference, 0.1, false, 0.1);

    // 示例 3: 不规则地址 QSpinBox，默认值 0
    //        addMapping(505, ui->spinBox_Offset, 1.0, true, 0.0);
    //        addMapping(200, ui->spinBox_TargetTemp, 0.1, true);  // 寄存器255 -> 显示25.5
    //            addMapping(20, ui->spinBox_Repair_Button_Defrost_Curve_Point_A, 1.0, true, -6.0);     // 支持负数补偿
    //        addMapping(310, ui->spinBox_Limit, 1.0, false);    // 仅正数
}

void Modify_configuration_parameters::on_btnRead_clicked()
{
    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    for (const auto &config : m_configMappings) {
        QModbusDataUnit request(QModbusDataUnit::HoldingRegisters, config.address, 1);
        if (auto *reply = m_modbusClient->sendReadRequest(request, 1)) {
            reply->setProperty("addr", config.address); // 动态属性记录地址
            connect(reply, &QModbusReply::finished, this, &Modify_configuration_parameters::onReadFinished);
        }
    }
}

void Modify_configuration_parameters::onReadFinished()
{
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply) return;

    if (reply->error() == QModbusDevice::NoError) {
        // 获取该请求对应的寄存器地址（在发送请求时通过 setProperty 记录）
        int addr = reply->property("address").toInt();
        const QModbusDataUnit unit = reply->result();
        quint16 rawValue = unit.value(0);

        // 在映射表中查找对应的控件
        for (const auto &m : m_mappings) {
            if (m.address == addr) {
                // 处理有符号数转换
                double actualValue = m.isSigned ? static_cast<int16_t>(rawValue) : rawValue;
                // 将实际值（乘以因子）显示到控件，覆盖之前的默认值
                setWidgetValue(m.widget, actualValue * m.factor);
                break;
            }
        }
    }
    reply->deleteLater();
}

void Modify_configuration_parameters::on_btnWrite_clicked()
{
    // 此处实现批量写入逻辑，逻辑同你之前的 Dialog.cpp
    QMessageBox::information(this, tr("提示"), tr("配置已下发"));
}


void Modify_configuration_parameters::on_pushButton_Repair_Button_Application_clicked()
{
    if (!m_modbusClient) return;

    // 遍历所有映射关系，逐个下发修改后的值
    for (const auto &m : m_mappings) {
        // 1. 获取控件当前值（自动识别 SpinBox/LineEdit/ComboBox）
        double currentVal = getWidgetValue(m.widget);

        // 2. 反向计算寄存器原始值 (例如：显示 25.5 / 因子 0.1 = 255)
        // 使用 static_cast<int16_t> 确保负数能正确转为补码 quint16
        quint16 rawValue = static_cast<quint16>(static_cast<int16_t>(currentVal / m.factor));

        // 3. 构建写入单元 (保持型寄存器 HoldingRegisters)
        QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, m.address, 1);
        writeUnit.setValue(0, rawValue);

        // 4. 发送请求 (假设从站 ID 为 1)
        if (auto *reply = m_modbusClient->sendWriteRequest(writeUnit, 1)) {
            if (!reply->isFinished()) {
                connect(reply, &QModbusReply::finished, this, [reply, m]() {
                    if (reply->error() != QModbusDevice::NoError) {
                        qDebug() << QString("地址 %1 写入失败: %2").arg(m.address).arg(reply->errorString());
                    }
                    reply->deleteLater();
                });
            } else {
                reply->deleteLater();
            }
        }
    }

    QMessageBox::information(this, tr("提示"), tr("参数修改指令已全部发送！"));
}

// 注册映射关系
void Modify_configuration_parameters::addMapping(int addr, QWidget* w, double factor, bool isSigned,
                                         double defaultValue, double min, double max,
                                         const QMap<int, QString> &options) {
    if (auto* cb = qobject_cast<QComboBox*>(w)) {
        cb->clear();
        // 遍历选项包并填入
        QMapIterator<int, QString> i(options);
        while (i.hasNext()) {
            i.next();
            // i.value() 是显示的文字（如"开启"），i.key() 是背后的数值（如 1）
            cb->addItem(i.value(), i.key());
        }
    }
    // 1. 处理 QSpinBox 范围
    if (auto* sb = qobject_cast<QSpinBox*>(w)) {
        sb->setRange(static_cast<int>(min), static_cast<int>(max));
    }
    // 2. 处理 QDoubleSpinBox 范围
    else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
        dsb->setRange(min, max);
    }
    // 3. 处理 QLineEdit 范围 (使用验证器)
    else if (auto* le = qobject_cast<QLineEdit*>(w)) {
        // 创建验证器：设置范围、小数位数(1位)、父对象
        QDoubleValidator *validator = new QDoubleValidator(min, max, 1, le);
        validator->setNotation(QDoubleValidator::StandardNotation);
        le->setValidator(validator);
    }
    if (auto* le = qobject_cast<QLineEdit*>(w)) {
        // 创建验证器
        // 参数：最小值, 最大值, 小数点后位数, 父对象
        // 如果 factor 是 1.0，则 decimals 设为 0（整数）；如果是 0.1，设为 1（一位小数）
        int decimals = (factor < 1.0) ? 1 : 0;

        QDoubleValidator *validator = new QDoubleValidator(min, max, decimals, le);

        // StandardNotation 允许用户输入普通的十进制数字（防止出现科学计数法 E）
        validator->setNotation(QDoubleValidator::StandardNotation);

        le->setValidator(validator);
    }

    // 设置默认值
    setWidgetValue(w, defaultValue);

    // 存入列表
    m_mappings.append({addr, w, factor, isSigned, options, min, max});
}

// 统一设置数值
void Modify_configuration_parameters::setWidgetValue(QWidget* w, double val) {
    if (auto* cb = qobject_cast<QComboBox*>(w)) {
        // static_cast<int> 很重要，Modbus 寄存器通常匹配整数 Key
        int index = cb->findData(static_cast<int>(val));
        if (index != -1) {
            cb->setCurrentIndex(index);
        }
    }
    else if (auto* sb = qobject_cast<QSpinBox*>(w)) {
        sb->setValue(static_cast<int>(val));
    }
    else if (auto* le = qobject_cast<QLineEdit*>(w)) {
        for (const auto &m : m_mappings) {
            if (m.widget == w) {
                if (m.factor < 1.0) {
                    // 显示一位小数，例如 25.5
                    le->setText(QString::number(val, 'f', 1));
                } else {
                    // 显示整数，例如 25
                    le->setText(QString::number(static_cast<int>(val)));
                }
                return;
            }
        }
        // 如果没找到映射，默认按原样转换
        le->setText(QString::number(val));
    }
}

// 统一获取数值
double Modify_configuration_parameters::getWidgetValue(QWidget* w) {
    if (!w) return 0;

    // 处理 ComboBox：获取关联的数值 (Data)
    if (auto* cb = qobject_cast<QComboBox*>(w)) {
        return cb->currentData().toDouble();
    }
    // 处理 SpinBox
    else if (auto* sb = qobject_cast<QSpinBox*>(w)) {
        return static_cast<double>(sb->value());
    }
    // 处理 DoubleSpinBox
    else if (auto* dsb = qobject_cast<QDoubleSpinBox*>(w)) {
        return dsb->value();
    }
    // 处理 LineEdit：将文本转为数字
    else if (auto* le = qobject_cast<QLineEdit*>(w)) {
        return le->text().toDouble();
    }
    return 0;
}


void Modify_configuration_parameters::on_pushButton_Repair_Button_Close_clicked()
{
    close();
}

void Modify_configuration_parameters::on_pushButton_Repair_Button_Confirm_clicked()
{
    if (!m_modbusClient) return;

    // 遍历所有映射关系，逐个下发修改后的值
    for (const auto &m : m_mappings) {
        // 1. 获取控件当前值（自动识别 SpinBox/LineEdit/ComboBox）
        double currentVal = getWidgetValue(m.widget);

        // 2. 反向计算寄存器原始值 (例如：显示 25.5 / 因子 0.1 = 255)
        // 使用 static_cast<int16_t> 确保负数能正确转为补码 quint16
        quint16 rawValue = static_cast<quint16>(static_cast<int16_t>(currentVal / m.factor));

        // 3. 构建写入单元 (保持型寄存器 HoldingRegisters)
        QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, m.address, 1);
        writeUnit.setValue(0, rawValue);

        // 4. 发送请求 (假设从站 ID 为 1)
        if (auto *reply = m_modbusClient->sendWriteRequest(writeUnit, 1)) {
            if (!reply->isFinished()) {
                connect(reply, &QModbusReply::finished, this, [reply, m]() {
                    if (reply->error() != QModbusDevice::NoError) {
                        qDebug() << QString("地址 %1 写入失败: %2").arg(m.address).arg(reply->errorString());
                    }
                    reply->deleteLater();
                });
            } else {
                reply->deleteLater();
            }
        }
    }

    QMessageBox::information(this, tr("提示"), tr("参数修改指令已全部发送！"));
}




