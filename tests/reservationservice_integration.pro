QT += core gui network sql
TEMPLATE = app
TARGET = reservationservice_integration
CONFIG += console c++17 utf8_source
CONFIG -= app_bundle

INCLUDEPATH += ../adminserver/src ../common

SOURCES += \
    reservationservice_integration.cpp \
    ../adminserver/src/database.cpp \
    ../adminserver/src/services/sessionservice.cpp \
    ../adminserver/src/services/chargeservice.cpp \
    ../adminserver/src/services/reservationservice.cpp \
    ../adminserver/src/services/stationservice.cpp \
    ../adminserver/src/services/userservice.cpp \
    ../adminserver/src/services/reviewservice.cpp \
    ../adminserver/src/services/adminservice.cpp \
    ../adminserver/src/services/analyticsservice.cpp \
    ../adminserver/src/transport/requestdispatcher.cpp \
    ../adminserver/src/dispatch.cpp \
    ../common/tencentapi.cpp

unix {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8
}
