QT += core gui widgets network
TARGET = userclient
TEMPLATE = app
CONFIG += c++17
CONFIG += utf8_source

INCLUDEPATH += $$PWD/src $$PWD/../common

SOURCES += \
    src/main.cpp \
    src/client.cpp \
    src/userwindow.cpp \
    ../common/protocol.cpp

HEADERS += \
    src/client.h \
    src/userwindow.h \
    ../common/protocol.h \
    ../common/appstyle.h \
    ../common/uidialog.h

unix {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8
}
