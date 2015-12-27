#!/bin/sh

# Set the directory of your Arduino installation directory
ARDUINO_DIR=/home/boul/src/arduino-1.6.7

# Used by build.sh to find the arduino executable
ARDUINO_EXE=$ARDUINO_DIR/arduino

# Used by build.sh to start arduino build
SKETCH_NAME=tftoscillo.ino

# Used by run.sh to connect to board
SERIAL_PORT=/dev/ttyACM0
