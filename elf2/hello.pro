#-------------------------------------------------
#
# Project created by QtCreator 2026-05-24T22:52:31
#
#-------------------------------------------------

QT       += core gui widgets

TARGET = hello
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11
INCLUDEPATH += /home/elf/qt_ws/hello
SOURCES += \
        main.cpp \
        widget.cpp \
        CameraThread.cpp \
        RKNNModel.cpp \
        InferenceThread.cpp \
        MqttClient.cpp

HEADERS += \
        widget.h \
        CameraThread.h \
        RKNNModel.h \
        InferenceThread.h \
        MqttClient.h 
# ============ OpenCV ============
CONFIG += link_pkgconfig
PKGCONFIG += opencv4

FORMS += \
        widget.ui
# ============ RKNPU ============

LIBS += -L/usr/lib -lrknnrt -lmosquitto


# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
