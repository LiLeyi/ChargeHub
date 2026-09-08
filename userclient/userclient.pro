QT += core gui widgets network
TARGET = userclient
TEMPLATE = app
CONFIG += c++17
CONFIG += utf8_source

INCLUDEPATH += $$PWD/src $$PWD/../common

SOURCES += \
    src/main.cpp \
    src/client.cpp \
    src/usercontroller.cpp \
    src/pages/loginpage.cpp \
    src/pages/chargepage.cpp \
    src/pages/orderspage.cpp \
    src/pages/reservationspage.cpp \
    src/userwindow.cpp \
    ../common/protocol.cpp \
    ../common/tencentapi.cpp

HEADERS += \
    src/client.h \
    src/usercontroller.h \
    src/pages/loginpage.h \
    src/pages/chargepage.h \
    src/pages/orderspage.h \
    src/pages/reservationspage.h \
    src/userwindow.h \
    ../common/protocol.h \
    ../common/appstyle.h \
    ../common/uidialog.h \
    ../common/tencentapi.h

unix {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8
}
