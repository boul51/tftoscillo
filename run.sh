#!/bin/sh

source ./prefs.mk

echo "Using arduino from $ARDUINO_EXE"

# Kill process using serial port (if any)
PID=`lsof $SERIAL_PORT  | awk '{print $2}' | grep -v PID`

if [ x$PID != x ]; then
	echo "Killing process using serial port (PID $PID)"
	kill -9 $PID
fi

echo "Will build and upload program"

$ARDUINO_EXE --upload $SKETCH_NAME

if [ $? != 0 ]; then
	echo "arduino upload command failed !"
	exit
fi

minicom -D $SERIAL_PORT

exit 0

