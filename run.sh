source prefs.txt

echo "Using arduino from $ARDUINO_EXE"

$ARDUINO_EXE --upload $SKETCH_NAME

if [ $? != 0 ]; then
	echo "arduino upload command failed !"
	exit
fi

minicom -D $SERIAL_PORT

