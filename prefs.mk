#!/bin/sh

# Use arduino from built repo
ARDUINO_EXE=/usr/src/Arduino/build/linux/work/arduino

# Use arduino from downloaded package
#ARDUINO_EXE=/opt/arduino-1.5.8/arduino

# Directory for Arduino user libraries
ARDUINO_USER_LIBDIR=~/Arduino/libraries

SKETCH_NAME=tftoscillo.ino

SERIAL_PORT=/dev/ttyACM0
