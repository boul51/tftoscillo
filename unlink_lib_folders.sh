#!/bin/sh

source ./prefs.mk

LIBS="GenSigDma AdcDma LibDbg"

for LIB in $LIBS; do
	rm $ARDUINO_USER_LIBDIR/$LIB
done

