QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

QT += charts
QT += serialport
QT += serialbus
QT += widgets
QT += sql

CONFIG += c++11

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    sqliterecorder.cpp \
    playbacksettingsdialog.cpp \
    adjustcompressor.cpp \
    compressor_status.cpp \
    curvedatabuffer.cpp \
    curvesdialog.cpp \
    curveswidget.cpp \
    main.cpp \
    modify_configuration_parameters.cpp \
    ofile2.cpp \
    other_data_items.cpp \
    pushbutton_parameters.cpp \
    single_valve_command.cpp \
    special_command.cpp \
    unlock.cpp \
    valvecommand.cpp

HEADERS += \
    sqliterecorder.h \
    playbacksettingsdialog.h \
    adjustcompressor.h \
    compressor_status.h \
    curvedatabuffer.h \
    curvesdialog.h \
    curveswidget.h \
    layer.h \
    modify_configuration_parameters.h \
    ofile2.h \
    other_data_items.h \
    pushbutton_parameters.h \
    single_valve_command.h \
    special_command.h \
    unlock.h \
    valvecommand.h

FORMS += \
    adjustcompressor.ui \
    compressor_status.ui \
    modify_configuration_parameters.ui \
    ofile2.ui \
    other_data_items.ui \
    pushbutton_parameters.ui \
    single_valve_command.ui \
    special_command.ui \
    unlock.ui \
    valvecommand.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES +=
