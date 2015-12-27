TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

include(prefs.mk)
include(deployment.pri)

qtcAddDeployment()

# Obtained by running qmake -pro in arduino folder..

INCLUDEPATH += ../GenSigDma \
               ../AdcDma \
               ../LibDbg \
               ../ArduinoSerialCommand \
               $$ARDUINO_DIR/hardware/arduino/avr/cores/arduino \
               $$ARDUINO_DIR/hardware/arduino/avr/libraries/SPI \
               $$ARDUINO_DIR/libraries/TFT/src

HEADERS += ../GenSigDma/*.h \
           ../AdcDma/*.h \
           ../LibDbg/*.h \
           ../ArduinoSerialCommand/*.h

SOURCES += tftoscillo.ino \
           ../GenSigDma/*.cpp \
           ../AdcDma/*.cpp \
           ../LibDbg/*.cpp \
           ../ArduinoSerialCommand/*.cpp
