#include "special_command.h"
#include "ui_special_command.h"
#include "ofile2.h"
#include <QModbusReply>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>

Special_command::Special_command(QModbusClient *modbusClient, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Special_command),
  m_modbusClient(modbusClient) // 初始化指针
{
    ui->setupUi(this);
    this->setFixedSize(688,625);
    setupRegisterMapping(); // 初始化地址映射
}

Special_command::~Special_command()
{
    delete ui;
}


// 【第一步】在这里添加你不连续的地址和对应的 LineEdit
void Special_command::setupRegisterMapping()
{
    m_regConfigs.clear();
    // 假设 UI 中的 ComboBox 命名为 comboBox_Defrost
    ui->comboBox->clear();
    ui->comboBox->addItem(tr("主机1"), 8); // 关联地址 8
    ui->comboBox->addItem(tr("从机0"), 9); // 关联地址 9
    ui->comboBox->addItem(tr("从机1"), 10);
    ui->comboBox->addItem(tr("从机2"), 11);
    ui->comboBox->addItem(tr("从机3"), 12);
    ui->comboBox->addItem(tr("从机4"), 13);
    ui->comboBox->addItem(tr("从机5"), 14);
    ui->comboBox->addItem(tr("从机6"), 15);
    ui->comboBox->addItem(tr("从机7"), 16);

    // 模仿 MainWindow 的映射方式
    // 即使地址是 1000, 1050, 1100 这种不连续的也可以
    m_regConfigs.append({1159, ui->lineEdit_19});
    m_regConfigs.append({1160, ui->lineEdit_20});
    m_regConfigs.append({1161, ui->lineEdit_21});
    m_regConfigs.append({1162, ui->lineEdit_22});
    m_regConfigs.append({1163, ui->lineEdit_23});
    m_regConfigs.append({1164, ui->lineEdit_24});
    m_regConfigs.append({1165, ui->lineEdit_25});
    m_regConfigs.append({1166, ui->lineEdit_26});
    m_regConfigs.append({1167, ui->lineEdit_27});
    m_regConfigs.append({1168, ui->lineEdit_28});
    m_regConfigs.append({1169, ui->lineEdit_29});
    m_regConfigs.append({1170, ui->lineEdit_30});
    m_regConfigs.append({1171, ui->lineEdit_31});
    m_regConfigs.append({1172, ui->lineEdit_32});
    m_regConfigs.append({1173, ui->lineEdit_33});
    m_regConfigs.append({1174, ui->lineEdit_34});
    m_regConfigs.append({1175, ui->lineEdit_35});
    m_regConfigs.append({1176, ui->lineEdit_36});
    ////////////制冷//////////////////////////////
    m_regConfigs.append({1177, ui->lineEdit_46});
    m_regConfigs.append({1178, ui->lineEdit_47});
    m_regConfigs.append({1179, ui->lineEdit_48});
    m_regConfigs.append({1180, ui->lineEdit_49});
    m_regConfigs.append({1181, ui->lineEdit_50});
    m_regConfigs.append({1182, ui->lineEdit_51});
    m_regConfigs.append({1183, ui->lineEdit_52});
    m_regConfigs.append({1184, ui->lineEdit_53});
    m_regConfigs.append({1185, ui->lineEdit_54});
    m_regConfigs.append({1186, ui->lineEdit_55});
    m_regConfigs.append({1187, ui->lineEdit_56});
    m_regConfigs.append({1188, ui->lineEdit_57});
    ////////////调频点////////////////////////////
    m_regConfigs.append({1189, ui->lineEdit_58});
    m_regConfigs.append({1190, ui->lineEdit_59});
    m_regConfigs.append({1191, ui->lineEdit_60});
    m_regConfigs.append({1192, ui->lineEdit_61});
    m_regConfigs.append({1193, ui->lineEdit_62});
    m_regConfigs.append({1194, ui->lineEdit_63});
    m_regConfigs.append({1195, ui->lineEdit_64});
    m_regConfigs.append({1196, ui->lineEdit_65});
    m_regConfigs.append({1197, ui->lineEdit_66});
    m_regConfigs.append({1198, ui->lineEdit_67});




}

void Special_command::on_btnRead_clicked()
{
    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

    // 获取主界面的站号设置 (假设 MainWindow 是父窗口)
     Ofile2 *mainWin = qobject_cast<Ofile2*>(this->parentWidget());
    int slaveId = 1;

    if (mainWin) {
        // 这里需要你在 mainwindow.h 里把站号控件设为 public 或者写一个 getSlaveId 函数
        // 暂时假设站号是 1，如果不是，请务必修改这里
    }

    for (const auto &config : m_regConfigs) {
            QModbusDataUnit readUnit(QModbusDataUnit::HoldingRegisters, config.address, 1);
            // 这里原先可能是 mainWin->getSlaveId()，现在直接写 1
            if (auto *reply = m_modbusClient->sendReadRequest(readUnit, slaveId)) {
                reply->setProperty("addr", config.address);
                connect(reply, &QModbusReply::finished, this, &Special_command::onReadFinished);
            }
        }
}

// 【第三步】回调解析
void Special_command::onReadFinished()
{
    auto reply = qobject_cast<QModbusReply *>(sender());
    if (!reply) return;

    if (reply->error() != QModbusDevice::NoError) {
        // 【关键】打印具体的错误信息到调试输出
        qDebug() << "Modbus Error:" << reply->errorString();
        // 或者弹窗显示
        // QMessageBox::warning(this, "错误", "读取失败: " + reply->errorString());
    }else{
        const QModbusDataUnit unit = reply->result();
        // 获取发请求时存入的地址
        int addr = reply->property("addr").toInt();
        int val = unit.value(0);

        // 查找该地址对应哪个 LineEdit 并更新
        for (const auto &config : m_regConfigs) {
            if (config.address == addr) {
                config.lineEdit->setText(QString::number(val));
                break;
            }
        }
    }
    reply->deleteLater();
}

void Special_command::on_btnModify_clicked()
{
    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) return;

        for (const auto &config : m_regConfigs) {
            QString text = config.lineEdit->text();
            if (text.isEmpty()) continue; // 没填的不处理

            bool ok;
            int val = text.toInt(&ok);
            if (!ok) continue;

            QModbusDataUnit writeUnit(QModbusDataUnit::HoldingRegisters, config.address, 1);
            writeUnit.setValue(0, static_cast<quint16>(val));

            if (auto *reply = m_modbusClient->sendWriteRequest(writeUnit, 1)) {
                connect(reply, &QModbusReply::finished, this, [reply, config]() {
                    if (reply->error() != QModbusDevice::NoError) {
                        qDebug() << "写入地址" << config.address << "失败:" << reply->errorString();
                    }
                    reply->deleteLater();
                });
            }
        }
        QMessageBox::information(this, tr("提示"), tr("批量修改指令已下发"));
}

void Special_command::on_pushButton_manual_defrost_clicked()
{
    // 1. 首先检查连接状态
    if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) {
        QMessageBox::warning(this, tr("警告"), tr("未连接 Modbus 设备"));
        return;
    }

    // 2. 【新增：确认弹窗】
    // 获取当前下拉框选中的名称，用于提示语
    QString deviceName = ui->comboBox->currentText();

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, tr("确认操作"),
                                  QString(tr("确定要对【%1】执行手动除霜操作吗？\n该操作会发送脉冲指令并持续2秒。")).arg(deviceName),
                                  QMessageBox::Yes | QMessageBox::No);

    // 如果用户点击了“否”，则直接退出函数，不执行后续逻辑
    if (reply == QMessageBox::No) {
        return;
    }

    int slaveId = 1;
    // 3. 执行原有逻辑：获取地址并发送指令
    int targetAddr = ui->comboBox->currentData().toInt();


    QModbusDataUnit writeUnit(QModbusDataUnit::Coils, targetAddr, 1);
    writeUnit.setValue(0, 1);

    // 禁用按钮防止重复点击
    ui->pushButton_manual_defrost->setEnabled(false);

    if (auto *replyPtr = m_modbusClient->sendWriteRequest(writeUnit, slaveId)) {
        connect(replyPtr, &QModbusReply::finished, this, [replyPtr, this, targetAddr, slaveId]() {
            if (replyPtr->error() == QModbusDevice::NoError) {
                qDebug() << "除霜指令(1)已发送至地址：" << targetAddr;

                // 脉冲逻辑：2秒后自动写 0
                QTimer::singleShot(2000, this, [this, targetAddr, slaveId]() {
                    QModbusDataUnit writeOff(QModbusDataUnit::Coils, targetAddr, 1);
                    writeOff.setValue(0, 0);

                    if (auto *offReply = m_modbusClient->sendWriteRequest(writeOff, slaveId)) {
                        connect(offReply, &QModbusReply::finished, offReply, &QObject::deleteLater);
                        // 脉冲结束后恢复按钮可用状态
                        ui->pushButton_manual_defrost->setEnabled(true);
                        qDebug() << "脉冲结束：地址" << targetAddr << "已自动复位为 0";
                    }
                });

            } else {
                QMessageBox::critical(this, tr("错误"), replyPtr->errorString());
                ui->pushButton_manual_defrost->setEnabled(true); // 失败了也要恢复按钮
            }
            replyPtr->deleteLater();
        });
    } else {
        ui->pushButton_manual_defrost->setEnabled(true);
    }
}


void Special_command::on_pushButton_5_clicked()
{
    // 1. 检查连接状态
        if (!m_modbusClient || m_modbusClient->state() != QModbusDevice::ConnectedState) {
            QMessageBox::warning(this, tr("警告"), tr("未连接设备，无法执行清零操作"));
            return;
        }

        // 2. 确认弹窗
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("确认清零"),
                                      tr("确定要清零【压缩机运行时间】吗？\n该操作将发送脉冲指令（地址1750）。"),
                                      QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) return;

        // 3. 准备参数
        int targetAddr = 1750; // 目标地址
        int slaveId = 1;      // 站号
        ui->pushButton_5->setEnabled(false); // 禁用按钮防止重复触发

        // 4. 发送“置 1”请求
        // 注意：这里假设 1750 是线圈(Coils)，如果是保持寄存器请改为 HoldingRegisters
        QModbusDataUnit writeOn(QModbusDataUnit::HoldingRegisters, targetAddr, 1);
        writeOn.setValue(0, 1);

        if (auto *replyPtr = m_modbusClient->sendWriteRequest(writeOn, slaveId)) {
            connect(replyPtr, &QModbusReply::finished, this, [this, replyPtr, targetAddr, slaveId]() {
                if (replyPtr->error() == QModbusDevice::NoError) {
                    qDebug() << "清零指令(1)已发送";

                    // 5. 【脉冲逻辑】：2秒后自动写 0
                    QTimer::singleShot(2000, this, [this, targetAddr, slaveId]() {
                        QModbusDataUnit writeOff(QModbusDataUnit::Coils, targetAddr, 1);
                        writeOff.setValue(0, 0); // 复位为 0

                        if (auto *offReply = m_modbusClient->sendWriteRequest(writeOff, slaveId)) {
                            connect(offReply, &QModbusReply::finished, this, [this, offReply]() {
                                offReply->deleteLater();
                                ui->pushButton_5->setEnabled(true); // 脉冲彻底结束，恢复按钮
                                qDebug() << "脉冲结束：运行时间清零地址已复位为 0";
                            });
                        } else {
                            ui->pushButton_5->setEnabled(true);
                        }
                    });

                } else {
                    QMessageBox::critical(this, tr("错误"), tr("指令发送失败: ") + replyPtr->errorString());
                    ui->pushButton_5->setEnabled(true);
                }
                replyPtr->deleteLater();
            });
        } else {
            ui->pushButton_5->setEnabled(true);
        }
}

void Special_command::on_pushButton_8_clicked()
{
    close();
}
