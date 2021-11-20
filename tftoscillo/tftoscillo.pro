TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

!exists(tftoscillo.pri) {
    warning("tftoscillo.pri, source completion will failed to find arduino include files")
    warning("Please create a tftoscillo.pri file, with the following directive, adjusted to your sam version:")
    warning("SAMDIR=$(HOME)/.arduino15/packages/arduino/hardware/sam/1.6.12/")
}

!isEmpty(SAMDIR):!exists($$SAMDIR) {
    error("$$SAMDIR does not exist")
}

LIBDIR=$${PWD}/../lib

INCLUDEPATH += \
    $${SAMDIR}/cores/arduino \
    $${SAMDIR}/libraries/SPI \
    $${SAMDIR}/variants/arduino_due_x \
    $${LIBDIR}/GenSigDma \
    $${LIBDIR}/AdcDma \
    $${LIBDIR}/LibDbg \
    $${LIBDIR}/OscDisplay \
    $${LIBDIR}/OscDisplayDriver \
    $${LIBDIR}/ArduinoSerialCommand \
    $${LIBDIR}/TFT/src \
    $${LIBDIR}/Arduino-PrintStream/src \
    $${LIBDIR}/MemoryFree \

HEADERS += \
    $${LIBDIR}/GenSigDma/*.h \
    $${LIBDIR}/AdcDma/*.h \
    $${LIBDIR}/LibDbg/*.h \
    $${LIBDIR}/OscDisplay/*.h \
    $${LIBDIR}/OscDisplayDriver/*.h \
    $${LIBDIR}/ArduinoSerialCommand/*.h \
    $${LIBDIR}/TFT/src/*.h \
    $${LIBDIR}/Arduino-PrintStream/src/*.h \
    $${LIBDIR}/MemoryFree/*.h \

SOURCES += \
    tftoscillo.ino \
    $${LIBDIR}/GenSigDma/*.cpp \
    $${LIBDIR}/AdcDma/*.cpp \
    $${LIBDIR}/LibDbg/*.cpp \
    $${LIBDIR}/OscDisplay/*.cpp \
    $${LIBDIR}/OscDisplayDriver/*.cpp \
    $${LIBDIR}/ArduinoSerialCommand/*.cpp \
    $${LIBDIR}/TFT/src/*.cpp \
    $${LIBDIR}/Arduino-PrintStream/src/*.cpp \
    $${LIBDIR}/MemoryFree/*.cpp \

OTHER_FILES += \
    Makefile \
    tftoscillo.pri \
