#ifndef COMPRESSOR_STATUS_H
#define COMPRESSOR_STATUS_H

#include <QDialog>

namespace Ui {
class Compressor_Status;
}

class Compressor_Status : public QDialog
{
    Q_OBJECT

public:
    explicit Compressor_Status(QWidget *parent = nullptr);
    ~Compressor_Status();


private:
    Ui::Compressor_Status *ui;
};

#endif // COMPRESSOR_STATUS_H
