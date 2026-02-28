#ifndef MODIFY_CONFIGURATION_PARAMETERS_H
#define MODIFY_CONFIGURATION_PARAMETERS_H

#include <QDialog>
#include <QModbusClient>
#include <QModbusDataUnit>
#include <QMessageBox>
#include <QLineEdit>
#include <QList>
#include <QSpinBox>
#include <QDoubleSpinBox>

struct ConfigMapping {
    int address;
    QWidget *widget;   // 支持 QSpinBox 或 QDoubleSpinBox
    double factor;     // 缩放倍数 (如 0.1)
    bool isSigned;     // 是否支持负数 (int16_t)
    QMap<int, QString> comboOptions;
    double min;  // 【新增】最小值
    double max;  // 【新增】最大值
};

namespace Ui {
class Modify_configuration_parameters;
}

struct ConfigRegMapping {
    int address;          // Modbus 寄存器地址
    QLineEdit *lineEdit;  // 对应的 UI 输入框
    QSpinBox *spinBox = nullptr;
    double multiplier = 1.0; // 缩放倍数，例如：寄存器存 255，SpinBox 显示 25.5，则 multiplier 为 0.1
};


class Modify_configuration_parameters : public QDialog
{
    Q_OBJECT

public:
    explicit Modify_configuration_parameters (QModbusClient *modbusClient, QWidget *parent = nullptr);
    ~Modify_configuration_parameters();

private slots:
    void on_btnRead_clicked();    // 读取配置按钮
    void on_btnWrite_clicked();   // 写入并保存按钮
    void onReadFinished();        // 读取回调

    void on_pushButton_Repair_Button_Application_clicked();

    void on_pushButton_Repair_Button_Confirm_clicked();

    void on_pushButton_Repair_Button_Close_clicked();

private:
    Ui::Modify_configuration_parameters *ui;
    QModbusClient *m_modbusClient;
    QList<ConfigMapping> m_mappings;
    QList<ConfigRegMapping> m_configMappings;
    void setupMapping();          // 初始化地址映射
    void addMapping(int addr, QWidget* w, double factor = 1.0, bool isSigned = true,
                    double defaultValue = 0.0, double min = -32768.0, double max = 32767.0,
                    const QMap<int, QString> &options = {});
    void setWidgetValue(QWidget* w, double val);
    double getWidgetValue(QWidget* w);
};

#endif // MODIFY_CONFIGURATION_PARAMETERS_H
