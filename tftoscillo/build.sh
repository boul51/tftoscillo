#!/bin/sh

source ./prefs.mk

$ARDUINO_EXE --verify --board $BOARD_NAME $SKETCH_NAME

if [ $? != 0 ]; then
	echo "arduino build command failed !"
	exit
fi

