#!/bin/sh

echo "I'm in dir: $PWD"

source ./prefs.mk

echo "Got prefs"

pid=$$
ppid=$(grep PPid: /proc/${pid}/status | awk '{print $2'})
pppid=$(grep PPid: /proc/${ppid}/status | awk '{print $2'})

echo "My pid is $PID"
echo "Parent pid is $ppid"
echo "Grand Parent pid is $pppid"

echo "Using arduino from $ARDUINO_EXE"

# Kill processes named xterm containing run.sh
PID=`ps -ef | grep xterm | grep -v grep | grep -v $ppid | grep -v $pppid | grep run.sh | awk '{print $2'}`

if [ x$PID != x ]; then
        echo "Killing running script instances (PID $PID)"
        kill -9 $PID
fi

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

#xterm -

minicom -D $SERIAL_PORT

#./serial_stty.sh
#cat $SERIAL_PORT

exit 0

