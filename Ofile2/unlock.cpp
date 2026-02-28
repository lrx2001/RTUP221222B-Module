#include "unlock.h"
#include "ui_unlock.h"

Unlock::Unlock(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::Unlock)
{
    ui->setupUi(this);
}

Unlock::~Unlock()
{
    delete ui;
}

void Unlock::on_pushButton_2_clicked()
{
    this->close();
}
