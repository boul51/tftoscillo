#!/bin/sh

source ./prefs.mk

ln -s $PWD/GenSigDma/ $ARDUINO_USER_LIBDIR/GenSigDma
ln -s $PWD/AdcDma/ $ARDUINO_USER_LIBDIR/AdcDma

