#!/bin/sh

source ./prefs.mk

LIBS="GenSigDma AdcDma LibDbg"

for LIB in $LIBS; do
	ln -s $PWD/$LIB/ $ARDUINO_USER_LIBDIR/$LIB
done

