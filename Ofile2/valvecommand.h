#ifndef VALVECOMMAND_H
#define VALVECOMMAND_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QModbusClient>
#include <QDoubleValidator>
#include <QList>
#include <QTimer>
#include <QProgressBar> // 必须引入

struct ValveCmdMapping {
    int readAddress;    // InputRegisters 读取显示地址
    int writeAddress;   // HoldingRegisters 写入地址
    int bitAddress;     // 位控制寄存器 (如 30030)
    int bitPosition;    // 对应的比特位 (0, 1, 2...)
    QLineEdit *lineEdit;
    QPushButton *button;

    ValveCmdMapping(int r, int w, int bAddr, int bPos, QLineEdit* le,QPushButton* btn)
        :readAddress(r), writeAddress(w), bitAddress(bAddr), bitPosition(bPos), lineEdit(le), button(btn) {}
};

namespace Ui {
class ValveCommand;
}

class ValveCommand : public QDialog
{
    Q_OBJECT

public:
    explicit ValveCommand(QModbusClient *modbusClient, QWidget *parent = nullptr);
    void readAllValves();
    ~ValveCommand();


private slots:
    void onWriteButtonClicked();   // 统一的按钮点击处理
    void onReadFinished();        // 处理读取结果回调
    // --- 新增：主按钮点击槽函数 ---
    void on_pushButton_ApplyAll_clicked();
    // --- 新增：定时器触发执行 ---
    void executeNextButtonClick();

    void on_pushButton_Cancel_clicked();

private:
    Ui::ValveCommand *ui;
    QModbusClient *m_modbusClient;
    QList<ValveCmdMapping> m_valveMappings;
    QTimer *m_autoClickTimer;
        int m_clickIndex;

    void setupValves();
    void addValve(int r, int w, int bAddr, int bPos, QLineEdit* le, QPushButton* btn);
    void writeBitwiseRegister(int regAddr, int bitPos, bool setOne);
};

#endif // VALVECOMMAND_H
