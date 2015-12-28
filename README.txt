To use build.sh and run.sh scripts :
Modify ARDUINO_DIR in prefs.mk to point to your arduino installation directory

To edit project in qtCreator :
 - cd tftoscillo
 - qtcreator tftoscillo.pro
 - select at least one kit in "Configure Project"
 - in build settings:
   - disable shadow build
   - remove all build steps and add a new custom step with command build.sh
   - remove all clean steps and add a new custom step with command clean.sh
 - in run settings:
   - add a run configuration (custom executable)
   - set run.sh for executable name
 - build and run commands should then be available

You should then be able to build and run the project from qtcreator

Tips:

To disable verification after upload, modify the file
~/.arduino15/packages/arduino/hardware/sam/1.6.6/platform.txt
to remove the -v option in the upload.pattern line :
tools.bossac.upload.pattern="{path}/{cmd}" {upload.verbose} --port={serial.port.file} -U {upload.native_usb} -e -w -v -b "{build.path}/{build.project_name}.bin" -R
becomes
tools.bossac.upload.pattern="{path}/{cmd}" {upload.verbose} --port={serial.port.file} -U {upload.native_usb} -e -w -b "{build.path}/{build.project_name}.bin" -R
