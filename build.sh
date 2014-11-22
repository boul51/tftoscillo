source prefs.txt

$ARDUINO_EXE --verify $SKETCH_NAME

if [ $? != 0 ]; then
	echo "arduino build command failed !"
	exit
fi

