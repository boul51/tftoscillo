This project is meant to make a pocket oscillator from an Arduino Due board along with a TFT screen.

To build,clean and run the project,
use associated Makefile targets in tftoscillo subfolder

You must have arduino-cli in your path (available from https://github.com/arduino/arduino-cli.git)

Note: Arduino IDE cannot be used to build the project because the libraries are not in the standard path.
If you want to use Arduino IDE, add links to the folders in lib folder in your ~/Arduino/libraries folder.

To edit project in qtCreator :
 - cd tftoscillo
 - qtcreator tftoscillo.pro
 - select at least one kit in "Configure Project"
 - in build settings:
   - disable shadow build
 - in run settings:
   - add a run configuration (custom executable)
   - set run.sh for executable name

You should then be able to build and run the project from qtcreator
