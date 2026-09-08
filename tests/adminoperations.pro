QT += core gui widgets network sql testlib
TEMPLATE = app
TARGET = testadminoperations
CONFIG += c++17 testcase utf8_source
INCLUDEPATH += $$PWD/../adminserver/src $$PWD/../common

SOURCES += \
    $$PWD/testadminoperations.cpp \
    $$PWD/../adminserver/src/database.cpp \
    $$PWD/../adminserver/src/dispatch.cpp \
    $$PWD/../adminserver/src/mainwindow.cpp \
    $$PWD/../adminserver/src/chartwidget.cpp \
    $$PWD/../adminserver/src/services/sessionservice.cpp \
    $$PWD/../adminserver/src/services/reservationservice.cpp \
    $$PWD/../adminserver/src/services/chargeservice.cpp \
    $$PWD/../adminserver/src/services/stationservice.cpp \
    $$PWD/../adminserver/src/services/userservice.cpp \
    $$PWD/../adminserver/src/services/reviewservice.cpp \
    $$PWD/../adminserver/src/services/adminservice.cpp \
    $$PWD/../adminserver/src/services/analyticsservice.cpp \
    $$PWD/../adminserver/src/transport/requestdispatcher.cpp \
    $$PWD/../common/protocol.cpp \
    $$PWD/../common/tencentapi.cpp

HEADERS += \
    $$PWD/../adminserver/src/mainwindow.h \
    $$PWD/../adminserver/src/chartwidget.h

unix: QMAKE_CXXFLAGS += -finput-charset=UTF-8
