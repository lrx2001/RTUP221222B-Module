#include "ofile2.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Ofile2 w;
    w.show();
    return a.exec();
}
