TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

include(prefs.mk)

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
