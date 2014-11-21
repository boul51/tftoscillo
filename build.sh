/opt/arduino-1.5.8/arduino --upload tftoscillo.ino
if [ $? != 0 ]; then
	echo "arduino upload command failed !"
	exit
fi
minicom -D /dev/ttyACM0
#tail -f /dev/ttyACM0

