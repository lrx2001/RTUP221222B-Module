#include "single_valve_command.h"
#include "ui_single_valve_command.h"
#include <QModbusReply>
#include <QMessageBox>
#include <QDebug>

Single_valve_command::Single_valve_command(QModbusClient *modbusClient, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Single_valve_command),
    m_modbusClient(modbusClient)
{
    ui->setupUi(this);
    setupValves();
    // 进入窗口时，如果连接着，就更新一次界面
    if (m_modbusClient && m_modbusClient->state() == QModbusDevice::ConnectedState) {
        readAllValves();
    }
}


void Single_valve_command::addValve(int r, int w, int bAddr, int bPos, QLineEdit* le, QPushButton* btn) {
    if (le->text().isEmpty()) le->setText("0");
    le->setValidator(new QIntValidator(0, 10000, le));
    connect(btn, &QPushButton::clicked, this, &Single_valve_command::onWriteButtonClicked);
    m_valveMappings.append(ValveMapping(r, w, bAddr, bPos, le, btn));
}

void Single_valve_command::setupValves() {
    m_valveMappings.clear();
    // 参数：读地址, 写地址, 位寄存器地址, 位偏移量, LineEdit, Button
    // 比如：第一个阀门绑定 30030 的 bit 0
    addValve(20, 30043, 30030,0, ui->lineEdit_single_Button_System_1_Main_Valve, ui->pushButton_single_Button_Master_modify1);
    addValve(21, 30044, 30030,1, ui->lineEdit_single_Button_System_2_Main_Valve, ui->pushButton_single_Button_Master_modify2);
    addValve(24, 30045, 30030,2, ui->lineEdit_single_Button_System_3_Main_Valve, ui->pushButton_single_Button_Master_modify3);
    addValve(25, 30046, 30030,3, ui->lineEdit_single_Button_System_4_Main_Valve, ui->pushButton_single_Button_Master_modify4);
    addValve(22, 30049, 30030,8, ui->lineEdit_single_Button_System_1_Valve, ui->pushButton_single_Button_shop_modify1);
    addValve(23, 30050, 30030,9, ui->lineEdit_single_Button_System_2_valve, ui->pushButton_single_Button_shop_modify2);
    addValve(26, 30051, 30030,10, ui->lineEdit_single_Button_System_3_valve, ui->pushButton_single_Button_shop_modify3);
    addValve(27, 30052, 30030,11, ui->lineEdit_single_Button_System_4_valve, ui->pushButton_single_Button_shop_modify4);
    //    addValve(301, ui->lineEdit_2, ui->pushButton_2, 0.0);
}

void Single_valve_command::readAllValves() {

    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    for (const auto &m : m_valveMappings) {
        // 修改点：使用 InputRegisters 进行初始读取
        QModbusDataUnit request(QModbusDataUnit::InputRegisters, m.readAddress, 1);
        if (auto *reply = m_modbusClient->sendReadRequest(request, 1)) {
            reply->setProperty("readAddr", m.readAddress);
            connect(reply, &QModbusReply::finished, this, &Single_valve_command::onReadFinished);
        }
    }
}

void Single_valve_command::onReadFinished() {
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply || reply->error() != QModbusDevice::NoError) {
        if(reply) reply->deleteLater();
        return;
    }

    int rAddr = reply->property("readAddr").toInt();
    int val = reply->result().value(0);

    for (const auto &m : m_valveMappings) {
        if (m.readAddress == rAddr) {
            m.lineEdit->setText(QString::number(val));
            break;
        }
    }

    reply->deleteLater();
}

void Single_valve_command::onWriteButtonClicked() {
    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    QPushButton* clickedBtn = qobject_cast<QPushButton*>(sender());
    for (const auto &m : m_valveMappings) {
        if (m.button == clickedBtn) {
            int val = m.lineEdit->text().toInt();

            if (val == 500) {
                // 情况一：输入 500 -> 取消发送，对应的 bit 位置 0
                writeBitwiseRegister(m.bitAddress, m.bitPosition, false);
                QMessageBox::information(this, "模式切换", QString("地址 %1 已设为自动（Bit %2 已清零）").arg(m.bitAddress).arg(m.bitPosition));
            }
            else {
                // 情况二：正常修改 -> 发送数值指令，对应的 bit 位置 1
                if (QMessageBox::question(this, "确认修改", QString("将值设为 %1 并切换为手动吗？").arg(val)) == QMessageBox::Yes) {

                    // 1. 发送数值到 Holding Register
                    QModbusDataUnit valUnit(QModbusDataUnit::HoldingRegisters, m.writeAddress, 1);
                    valUnit.setValue(0, static_cast<quint16>(val));
                    m_modbusClient->sendWriteRequest(valUnit, 1);

                    // 2. 将对应的 bit 位置 1
                    writeBitwiseRegister(m.bitAddress, m.bitPosition, true);
                }
            }
            break;
        }
    }
}

void Single_valve_command::writeBitwiseRegister(int regAddr, int bitPos, bool setOne) {
    // 1. 先读取当前该寄存器的 16 位值
    QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, regAddr, 1);
    if (auto *reply = m_modbusClient->sendReadRequest(readUnit, 1)) {
        connect(reply, &QModbusReply::finished, this, [=]() {
            if (reply->error() == QModbusDevice::NoError) {
                quint16 currentVal = reply->result().value(0);

                // 2. 修改特定位
                if (setOne) currentVal |= (1 << bitPos);  // 置 1
                else currentVal &= ~(1 << bitPos);       // 置 0

                // 3. 写回寄存器
                QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, regAddr, 1);
                writeUnit.setValue(0, currentVal);
                m_modbusClient->sendWriteRequest(writeUnit, 1);
            }
            reply->deleteLater();
        });
    }
}

Single_valve_command::~Single_valve_command()
{
    delete ui;
}
