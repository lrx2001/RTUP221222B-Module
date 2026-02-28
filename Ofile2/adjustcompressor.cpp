#include "adjustcompressor.h"
#include "ui_adjustcompressor.h"
#include <QModbusReply>
#include <QMessageBox>

AdjustCompressor::AdjustCompressor(QModbusClient *modbusClient, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::AdjustCompressor),
    m_modbusClient(modbusClient)
{
    ui->setupUi(this);
    setupValves();
    // 进入窗口时，如果连接着，就更新一次界面
    if (m_modbusClient && m_modbusClient->state() == QModbusDevice::ConnectedState) {
        readAllValves();
    }
}

void AdjustCompressor::addValve(int r, int w, int bAddr, int bPos, QLineEdit* le, QPushButton* btn) {
    if (le->text().isEmpty()) le->setText("0");
    le->setValidator(new QIntValidator(0, 10000, le));
    connect(btn, &QPushButton::clicked, this, &AdjustCompressor::onWriteButtonClicked);
    m_compressorMappings.append(CompressorMapping(r, w, bAddr, bPos, le, btn));
}

void AdjustCompressor::setupValves() {
    m_compressorMappings.clear();
    // 参数：读地址, 写地址, 位寄存器地址, 位偏移量, LineEdit, Button
    // 比如：第一个阀门绑定 30030 的 bit 0
    addValve(69, 30031, 30029,0, ui->lineEdit_Compressor1, ui->pushButton_Compressor1);
    addValve(70, 30032, 30029,1, ui->lineEdit_Compressor2, ui->pushButton_Compressor2);
    addValve(71, 30033, 30029,2, ui->lineEdit_Compressor3, ui->pushButton_Compressor3);
    addValve(72, 30034, 30029,3, ui->lineEdit_Compressor4, ui->pushButton_Compressor4);
    addValve(77, 30037, 30029,8, ui->lineEdit_RotationalSpeed1, ui->pushButton_RotationalSpeed1);
    addValve(78, 30038, 30029,9, ui->lineEdit_RotationalSpeed2, ui->pushButton_RotationalSpeed2);

    //    addValve(301, ui->lineEdit_2, ui->pushButton_2, 0.0);
}

void AdjustCompressor::readAllValves() {

    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    for (const auto &m : m_compressorMappings) {
        // 修改点：使用 InputRegisters 进行初始读取
        QModbusDataUnit request(QModbusDataUnit::InputRegisters, m.readAddress, 1);
        if (auto *reply = m_modbusClient->sendReadRequest(request, 1)) {
            reply->setProperty("readAddr", m.readAddress);
            connect(reply, &QModbusReply::finished, this, &AdjustCompressor::onReadFinished);
        }
    }
}

void AdjustCompressor::onReadFinished() {
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply || reply->error() != QModbusDevice::NoError) {
        if(reply) reply->deleteLater();
        return;
    }

    int rAddr = reply->property("readAddr").toInt();
    int val = reply->result().value(0);

    for (const auto &m : m_compressorMappings) {
        if (m.readAddress == rAddr) {
            m.lineEdit->setText(QString::number(val));
            break;
        }
    }

    reply->deleteLater();
}

void AdjustCompressor::onWriteButtonClicked() {
    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    QPushButton* clickedBtn = qobject_cast<QPushButton*>(sender());
    for (const auto &m : m_compressorMappings) {
        if (m.button == clickedBtn) {
            int val = m.lineEdit->text().toInt();

            if (val == 1234) {
                // 情况一：输入 1500 -> 取消发送，对应的 bit 位置 0
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

void AdjustCompressor::writeBitwiseRegister(int regAddr, int bitPos, bool setOne) {
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

AdjustCompressor::~AdjustCompressor()
{
    delete ui;
}
