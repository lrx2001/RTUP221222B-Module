#ifndef ADJUSTCOMPRESSOR_H
#define ADJUSTCOMPRESSOR_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QModbusClient>
#include <QDoubleValidator>

struct CompressorMapping {
    int readAddress;    // InputRegisters 读取地址
    int writeAddress;   // HoldingRegisters 写入地址
    int bitAddress;     // 比如 30030，控制手动/自动的寄存器
    int bitPosition;    // 对应的比特位 (0, 1, 2...)
    QLineEdit *lineEdit;
    QPushButton *button;

    CompressorMapping(int r, int w, int bAddr, int bPos, QLineEdit* le, QPushButton* btn)
        : readAddress(r), writeAddress(w), bitAddress(bAddr), bitPosition(bPos), lineEdit(le), button(btn) {}
};


namespace Ui {
class AdjustCompressor;
}

class AdjustCompressor : public QDialog
{
    Q_OBJECT

public:
    explicit AdjustCompressor(QModbusClient *modbusClient, QWidget *parent = nullptr);
    void readAllValves();        // 新增：一键读取所有地址
    ~AdjustCompressor();


private slots:
    void onWriteButtonClicked(); // 统一的按钮点击处理
    void onReadFinished();       // 新增：处理读取结果的回调

private:
    Ui::AdjustCompressor *ui;
    QModbusClient *m_modbusClient;
    QList<CompressorMapping> m_compressorMappings;

    void addValve(int r, int w, int bAddr, int bPos, QLineEdit* le, QPushButton* btn);
    void writeBitwiseRegister(int regAddr, int bitPos, bool setOne); // 辅助函数：操作位
    void setupValves();
};

#endif // ADJUSTCOMPRESSOR_H
