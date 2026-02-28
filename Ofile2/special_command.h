#ifndef SPECIAL_COMMAND_H
#define SPECIAL_COMMAND_H

#include <QDialog>
#include <QModbusDataUnit>
#include <QModbusClient>
#include <QMessageBox>
#include <QLineEdit>
#include <QList>

namespace Ui {
class Special_command;
}

// 定义一个简单的配置结构
struct DialogRegMapping {
    int address;          // Modbus 地址
    QLineEdit *lineEdit;  // 对应的 UI 控件
};

class Special_command : public QDialog
{
    Q_OBJECT

public:
    explicit Special_command(QModbusClient *modbusClient, QWidget *parent = nullptr);
    ~Special_command();

private slots:

    void onReadFinished();          // 统一处理读取回调
    void on_btnRead_clicked();

    void on_btnModify_clicked();

    void on_pushButton_manual_defrost_clicked();

    void on_pushButton_8_clicked();

    void on_pushButton_5_clicked();

private:
    Ui::Special_command *ui;
    QModbusClient *m_modbusClient; // 存储主窗口传来的 Modbus 指针
    QList<DialogRegMapping> m_regConfigs; // 存储不连续地址的列表
    void setupRegisterMapping();          // 初始化映射关系
};

#endif // SPECIAL_COMMAND_H
