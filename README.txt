This project is meant to make a pocket oscillator from an Arduino Due board along with a TFT screen.

To build,clean and run the project,
use associated Makefile targets in tftoscillo subfolder:
make, make clean, make run

You must have arduino-cli in your path (available from https://github.com/arduino/arduino-cli.git)

Note: Arduino IDE cannot be used to build the project because the libraries are not in the standard path,
and requires some extra defines to be set (SERIAL_IFACE=Serial or SERIAL_IFACE_SerialUSB and SERIALCOMMAND_HARDWAREONLY)
To overcome the libraries issue, you can add links to the folders in lib folder in your ~/Arduino/libraries folder,
but I don't know how to set those defines globally from the Arduino IDE.

To edit project in qtCreator :
 - cd tftoscillo
 - qtcreator tftoscillo.pro
 - select at least one kit in "Configure Project"
 - in build settings:
   - disable shadow build
 - in run settings:
   - enable run in terminal option

You should then be able to build and run the project from qtcreator

Check Makefile for additional options (in particular SERIAL_IFACE and DEBUG_CMD)
