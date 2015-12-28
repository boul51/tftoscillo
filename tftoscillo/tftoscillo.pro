TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

include(prefs.mk)

INCLUDEPATH += ../GenSigDma \
               ../AdcDma \
               ../LibDbg \
               ../ArduinoSerialCommand \
               $$SAM_DIR/packages/arduino/hardware/sam/1.6.6/cores/arduino \
               $$SAM_DIR/packages/arduino/hardware/sam/1.6.6/libraries/SPI \
               $$SAM_DIR/packages/arduino/hardware/sam/1.6.6/variants/arduino_due_x \
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
