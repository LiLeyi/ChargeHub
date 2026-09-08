QT += core gui widgets network sql
TARGET = adminserver
TEMPLATE = app
CONFIG += c++17
CONFIG += utf8_source

INCLUDEPATH += $$PWD/src $$PWD/../common

SOURCES += \
    src/main.cpp \
    src/database.cpp \
    src/services/sessionservice.cpp \
    src/services/chargeservice.cpp \
    src/services/reservationservice.cpp \
    src/services/stationservice.cpp \
    src/services/userservice.cpp \
    src/services/reviewservice.cpp \
    src/services/adminservice.cpp \
    src/services/analyticsservice.cpp \
    src/transport/requestdispatcher.cpp \
    src/dispatch.cpp \
    src/tcpserver.cpp \
    src/chartwidget.cpp \
    src/mainwindow.cpp \
    ../common/protocol.cpp \
    ../common/tencentapi.cpp

HEADERS += \
    src/database.h \
    src/services/sessionservice.h \
    src/services/chargeservice.h \
    src/services/reservationservice.h \
    src/services/stationservice.h \
    src/services/userservice.h \
    src/services/reviewservice.h \
    src/services/adminservice.h \
    src/services/analyticsservice.h \
    src/transport/serviceresult.h \
    src/transport/requestdispatcher.h \
    src/dispatch.h \
    src/tcpserver.h \
    src/chartwidget.h \
    src/mainwindow.h \
    ../common/protocol.h \
    ../common/appstyle.h \
    ../common/uidialog.h \
    ../common/tencentapi.h

unix {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8
}
