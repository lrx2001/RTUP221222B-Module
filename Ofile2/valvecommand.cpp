#include "valvecommand.h"
#include "ui_valvecommand.h"
#include <QModbusReply>
#include <QModbusDataUnit>
#include <QMessageBox>

ValveCommand::ValveCommand(QModbusClient *modbusClient, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ValveCommand),
    m_modbusClient(modbusClient)
{
    ui->setupUi(this);
    this->setFixedSize(282,196);
    ui->btn_hidden1->hide();
    ui->btn_hidden2->hide();
    ui->btn_hidden3->hide();
    ui->btn_hidden4->hide();

    setupValves();
    if (m_modbusClient && m_modbusClient->state() == QModbusDevice::ConnectedState) {
        readAllValves();
    }
    // 初始化进度条：设置范围 0-4
        ui->progressBar->setRange(0, m_valveMappings.size());
        ui->progressBar->setValue(0);
    // --- 新增：初始化计时器 ---
        m_autoClickTimer = new QTimer(this);
        connect(m_autoClickTimer, &QTimer::timeout, this, &ValveCommand::executeNextButtonClick);

}

// --- 新增：主按钮逻辑 ---
void ValveCommand::on_pushButton_ApplyAll_clicked()
{
    if (m_valveMappings.isEmpty()) return;

    // 重置索引，从第一个开始
    m_clickIndex = 0;

    ui->progressBar->setValue(0);
    // 禁用主确认按钮和窗口，防止连点导致逻辑混乱
    ui->pushButton_ApplyAll->setEnabled(false);

    // 启动计时器，每 500ms 触发一次
    m_autoClickTimer->start(1000);
}

// --- 新增：定时器点名执行 ---
void ValveCommand::executeNextButtonClick()
{
    // 如果已经点完了所有映射中的按钮
    if (m_clickIndex >= m_valveMappings.size()) {
        m_autoClickTimer->stop();
        ui->pushButton_ApplyAll->setEnabled(true); // 恢复按钮
        QMessageBox::information(this, "提示", "所有指令已按序下发完毕");
        return;
    }

    // 关键点：获取当前映射对应的 QPushButton，并执行模拟点击
    QPushButton *targetBtn = m_valveMappings[m_clickIndex].button;
    if (targetBtn) {
        // 调用 animateClick() 或 click()。animateClick 会有视觉上的按下效果
        targetBtn->animateClick();
    }

    // 索引指向下一个
    m_clickIndex++;
    ui->progressBar->setValue(m_clickIndex);
}

void ValveCommand::addValve(int r, int w, int bAddr, int bPos, QLineEdit* le, QPushButton* btn) {
    if (!le || !btn) return;
    // --- 关键修正：先断开旧连接，防止重连后一个按钮触发多次逻辑 ---
    disconnect(btn, &QPushButton::clicked, this, &ValveCommand::onWriteButtonClicked);
    if (le->text().isEmpty()) le->setText("0");
    le->setValidator(new QIntValidator(0, 10000, le));
    connect(btn, &QPushButton::clicked, this, &ValveCommand::onWriteButtonClicked);
    m_valveMappings.append(ValveCmdMapping(r, w, bAddr, bPos, le, btn));
}

void ValveCommand::setupValves() {
    m_valveMappings.clear();
    // 参数：读地址, 写地址, 位寄存器地址, 位偏移量, LineEdit, Button
    // 比如：第一个阀门绑定 30030 的 bit 0
    addValve(20, 30043, 30030,0, ui->lineEdit_EXV1, ui->btn_hidden1);
    addValve(21, 30044, 30030,1, ui->lineEdit_EXV2, ui->btn_hidden2);
    addValve(24, 30045, 30030,2, ui->lineEdit_EXV3, ui->btn_hidden3);
    addValve(25, 30046, 30030,3, ui->lineEdit_EXV4, ui->btn_hidden4);

}

void ValveCommand::readAllValves() {

    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    for (const auto &m : m_valveMappings) {
        // 修改点：使用 InputRegisters 进行初始读取
        QModbusDataUnit request(QModbusDataUnit::InputRegisters, m.readAddress, 1);
        if (auto *reply = m_modbusClient->sendReadRequest(request, 1)) {
            reply->setProperty("readAddr", m.readAddress);
            connect(reply, &QModbusReply::finished, this, &ValveCommand::onReadFinished);
        }
    }
}

void ValveCommand::onReadFinished() {
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

void ValveCommand::onWriteButtonClicked() {
    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    QPushButton* clickedBtn = qobject_cast<QPushButton*>(sender());
    for (const auto &m : m_valveMappings) {
        if (m.button == clickedBtn) {
            int val = m.lineEdit->text().toInt();

            if (val == 500) {
                // 情况一：输入 500 -> 取消发送，对应的 bit 位置 0
                writeBitwiseRegister(m.bitAddress, m.bitPosition, false);
//                QMessageBox::information(this, "模式切换", QString("地址 %1 已设为自动（Bit %2 已清零）").arg(m.bitAddress).arg(m.bitPosition));
            }
            else {
                // 情况二：正常修改 -> 发送数值指令，对应的 bit 位置 1
//                if (QMessageBox::question(this, "确认修改", QString("将值设为 %1 并切换为手动吗？").arg(val)) == QMessageBox::Yes) {

                    // 1. 发送数值到 Holding Register
                    QModbusDataUnit valUnit(QModbusDataUnit::HoldingRegisters, m.writeAddress, 1);
                    valUnit.setValue(0, static_cast<quint16>(val));
                    m_modbusClient->sendWriteRequest(valUnit, 1);

                    // 2. 将对应的 bit 位置 1
                    writeBitwiseRegister(m.bitAddress, m.bitPosition, true);
//                }
            }
            break;
        }
    }
}

void ValveCommand::writeBitwiseRegister(int regAddr, int bitPos, bool setOne) {
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

ValveCommand::~ValveCommand()
{
    if(m_autoClickTimer) m_autoClickTimer->stop();
    delete ui;
}

void ValveCommand::on_pushButton_Cancel_clicked()
{
    close();
}
