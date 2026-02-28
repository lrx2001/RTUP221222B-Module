#ifndef UNLOCK_H
#define UNLOCK_H

#include <QDialog>

namespace Ui {
class Unlock;
}

class Unlock : public QDialog
{
    Q_OBJECT

public:
    explicit Unlock(QWidget *parent = nullptr);
    ~Unlock();

private slots:
    void on_pushButton_2_clicked();

private:
    Ui::Unlock *ui;
};

#endif // UNLOCK_H
