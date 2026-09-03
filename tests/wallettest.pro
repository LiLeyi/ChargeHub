QT += core gui network sql testlib

TARGET = wallettest
TEMPLATE = app
CONFIG += console c++17 testcase
CONFIG -= app_bundle

INCLUDEPATH += $$PWD/../common $$PWD/../adminserver/src $$PWD/../userclient/src

SOURCES += \
    wallettest.cpp \
    ../adminserver/src/database.cpp \
    ../adminserver/src/dispatch.cpp \
    ../adminserver/src/rechargetransaction.cpp \
    ../adminserver/src/tcpserver.cpp \
    ../userclient/src/client.cpp \
    ../userclient/src/walletcontroller.cpp \
    ../common/protocol.cpp

HEADERS += \
    ../adminserver/src/database.h \
    ../adminserver/src/dispatch.h \
    ../adminserver/src/rechargetransaction.h \
    ../adminserver/src/tcpserver.h \
    ../userclient/src/client.h \
    ../userclient/src/walletcontroller.h \
    ../common/protocol.h
