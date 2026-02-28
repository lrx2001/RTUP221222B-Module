#ifndef PUSHBUTTON_PARAMETERS_H
#define PUSHBUTTON_PARAMETERS_H

#include <QDialog>
#include <QModbusClient>
#include <QModbusDataUnit>
#include <QMessageBox>
#include <QLineEdit>
#include <QList>
#include <QSpinBox>
#include <QDoubleSpinBox>

struct parametersMapping {
    int address;
    QWidget *widget;   // 支持 QSpinBox 或 QDoubleSpinBox
    double factor;     // 缩放倍数 (如 0.1)
    bool isSigned;     // 是否支持负数 (int16_t)
    QMap<int, QString> comboOptions;
    double min;  // 【新增】最小值
    double max;  // 【新增】最大值
};


namespace Ui {
class pushButton_parameters;
}

struct parametersRegMapping {
    int address;          // Modbus 寄存器地址
    QLineEdit *lineEdit;  // 对应的 UI 输入框
    QSpinBox *spinBox = nullptr;
    double multiplier = 1.0; // 缩放倍数，例如：寄存器存 255，SpinBox 显示 25.5，则 multiplier 为 0.1
};

class pushButton_parameters : public QDialog
{
    Q_OBJECT

public:
    explicit pushButton_parameters(QModbusClient *modbusClient, QWidget *parent = nullptr);
    ~pushButton_parameters();

private slots:
    void on_btnRead_clicked();    // 读取配置按钮
    void on_btnWrite_clicked();   // 写入并保存按钮
    void onReadFinished();        // 读取回调

    void on_pushButton_Repair_Button_Application_clicked();

    void on_pushButton_Repair_Button_Confirm_clicked();

    void on_pushButton_Repair_Button_Close_clicked();

private:
    Ui::pushButton_parameters *ui;
    QModbusClient *m_modbusClient;
    QList<parametersMapping> m_mappings;
    QList<parametersRegMapping> m_configMappings;
    void setupMapping();          // 初始化地址映射
    void addMapping(int addr, QWidget* w, double factor = 1.0, bool isSigned = true,
                    double defaultValue = 0.0, double min = -32768.0, double max = 32767.0,
                    const QMap<int, QString> &options = {});
    void setWidgetValue(QWidget* w, double val);
    double getWidgetValue(QWidget* w);
};

#endif // PUSHBUTTON_PARAMETERS_H
