QT += core gui widgets network sql
TARGET = adminserver
TEMPLATE = app
CONFIG += c++17
CONFIG += utf8_source

INCLUDEPATH += $$PWD/src $$PWD/../common

SOURCES += \
    src/main.cpp \
    src/database.cpp \
    src/dispatch.cpp \
    src/tcpserver.cpp \
    src/chartwidget.cpp \
    src/mainwindow.cpp \
    ../common/protocol.cpp

HEADERS += \
    src/database.h \
    src/dispatch.h \
    src/tcpserver.h \
    src/chartwidget.h \
    src/mainwindow.h \
    ../common/protocol.h \
    ../common/appstyle.h \
    ../common/uidialog.h

unix {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8
}
