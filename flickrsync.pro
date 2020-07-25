TEMPLATE = app
CONFIG += console
CONFIG += c++-11
CONFIG -= app_bundle

INCLUDEPATH += /usr/include/libxml2

LIBS += -lflickcurl -lxml2 -lcurl

SOURCES += \
    flickrsync.cpp
