#include "compressor_status.h"
#include "ui_compressor_status.h"
#include <QHeaderView>

Compressor_Status::Compressor_Status(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Compressor_Status)
{
    ui->setupUi(this);
    this->setFixedSize(600,290);

    ui->tableWidget->setRowCount(100); // 设置初始行数为5
//    ui->tableWidget->setStyleSheet(styles);
//    ui->tableWidget->horizontalHeader()->setStyleSheet(headerStyle);
    ui->tableWidget->setColumnWidth(0, 60);
    ui->tableWidget->setColumnWidth(1, 60);
    ui->tableWidget->setColumnWidth(2, 60);
    ui->tableWidget->setColumnWidth(3, 60);
    ui->tableWidget->setColumnWidth(4, 60);
    ui->tableWidget->setColumnWidth(5, 60);
    ui->tableWidget->setColumnWidth(6, 60);
    ui->tableWidget->setColumnWidth(7, 60);
    ui->tableWidget->setColumnWidth(8, 60);
    ui->tableWidget->setColumnWidth(9, 60);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    ui->tableWidget->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);//双击或获取焦点后单击，进入编辑状态
    ui->tableWidget->setEditTriggers(QAbstractItemView::NoEditTriggers); //不允许编辑
 }

Compressor_Status::~Compressor_Status()
{
    delete ui;
}
