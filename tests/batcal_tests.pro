QT += core gui dbus testlib

CONFIG += c++17 console
TARGET = batcal_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h
SOURCES += ../src/backend.cpp batcal_tests.cpp
