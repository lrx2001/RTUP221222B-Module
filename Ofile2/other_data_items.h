#ifndef OTHER_DATA_ITEMS_H
#define OTHER_DATA_ITEMS_H

#include <QDialog>

namespace Ui {
class Other_data_items;
}

class Other_data_items : public QDialog
{
    Q_OBJECT

public:
    explicit Other_data_items(QWidget *parent = nullptr);
    ~Other_data_items();

private:
    Ui::Other_data_items *ui;
};

#endif // OTHER_DATA_ITEMS_H
