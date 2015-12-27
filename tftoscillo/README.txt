To use build.sh and run.sh scripts :
Modify ARDUINO_DIR in prefs.mk to point to your arduino installation directory

To edit project in qtCreator :
 - qtcreator tftoscillo.pro
 - select at least one kit in "Configure Project"
 - in build settings:
   - disable shadow build
   - remove all build steps and add a new custom step with command build.sh
 - build and run commands should then be available

You should then be able to build and run the project from qtcreator

