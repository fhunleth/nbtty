TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.c \
    attach.c \
    master.c \
    ansi.c

HEADERS += \
    config.h \
    dtach.h \
    ansi.h
