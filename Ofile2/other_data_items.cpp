#include "other_data_items.h"
#include "ui_other_data_items.h"

Other_data_items::Other_data_items(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Other_data_items)
{
    ui->setupUi(this);
    this->setFixedSize(1585,761);
}

Other_data_items::~Other_data_items()
{
    delete ui;
}
