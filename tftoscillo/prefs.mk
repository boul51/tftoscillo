#!/bin/sh

# Use arduino from built repo
#ARDUINO_EXE=/usr/src/Arduino/build/linux/work/arduino

# Use arduino from downloaded package
#ARDUINO_EXE=~/src/arduino-1.6.7/arduino

# Use this to include arduino source libraries into qtcreator project file
#ARDUINO_SRC_DIR=/usr/src/Arduino

# Directory for Arduino user libraries
#ARDUINO_USER_LIBDIR=~/Arduino/libraries

ARDUINO_DIR=/home/boul/src/arduino-1.6.7

ARDUINO_EXE=$ARDUINO_DIR/arduino

SKETCH_NAME=tftoscillo.ino
#SKETCH_NAME=testAdcDma.ino

SERIAL_PORT=/dev/ttyACM0
