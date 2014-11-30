TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += main.c

include(deployment.pri)
qtcAddDeployment()

OTHER_FILES += \
    tftoscillo.ino

# Obtained by running qmake -pro in arduino folder..

INCLUDEPATH += ./GenSigDma \
               ./AdcDma \
               /usr/src/Arduino/build/linux/work/libraries/ArduinoSerialCommand \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/SoftwareSerial \
               /usr/src/Arduino/build/linux/work/libraries/Audio/src \
               /usr/src/Arduino/build/linux/work/libraries/Bridge/src \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/SPI \
               /usr/src/Arduino/build/linux/work/libraries/LiquidCrystal/src \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/Wire \
               /usr/src/Arduino/build/linux/work/libraries/Scheduler/src \
               /usr/src/Arduino/build/linux/work/libraries/SD/src \
               /usr/src/Arduino/build/linux/work/libraries/SD/src/utility \
               /usr/src/Arduino/build/linux/work/libraries/TFT/src \
               /usr/src/Arduino/build/linux/work/libraries/TFT/src/utility \
               /usr/src/Arduino/build/linux/work/libraries/USBHost/src \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/EEPROM \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/Wire/utility \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/cores/arduino \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/BOARDS \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/BOARDS/EVK1105 \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/UTILS \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/UTILS/PREPROCESSOR \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/BOARDS/ARDUINO \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/CMSIS/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include/component \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include/instance \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3s/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3s/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include/component \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include/instance \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3n/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3n/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include/component \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include/instance \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam4s/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam4s/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/libsam \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/libsam/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/variants/arduino_due_x \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ARM/ARMCM4/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ARM/ARMCM0/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ARM/ARMCM3/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/CMSIS/DSP_Lib/Examples/Common/Include \

INCLUDEPATH_ALL += ./GenSigDma \
               ./AdcDma \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/cores/arduino \
               /usr/src/Arduino/build/linux/work/hardware/tools/avr/avr/include \
               /usr/src/Arduino/build/linux/work/hardware/tools/avr/lib/gcc/avr/4.8.1/include \
               /usr/src/Arduino/build/linux/work/hardware/tools/gcc-arm-none-eabi-4.8.3-2014q1/arm-none-eabi/include/machine \
               /usr/src/Arduino/build/linux/work/hardware/tools/gcc-arm-none-eabi-4.8.3-2014q1/arm-none-eabi/include/sys \
               /usr/src/Arduino/build/linux/work/hardware/tools/gcc-arm-none-eabi-4.8.3-2014q1/arm-none-eabi/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/cores/arduino/avr \
               /usr/src/Arduino/build/linux/work/hardware/tools/avr/avr/include/avr \
               /usr/src/Arduino/build/linux/work/hardware/tools/avr/avr/include/util \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/variants/eightanaloginputs \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/variants/standard \
               /usr/src/Arduino/build/linux/work/libraries/ArduinoSerialCommand \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/SoftwareSerial \
               /usr/src/Arduino/build/linux/work/libraries/Audio/src \
               /usr/src/Arduino/build/linux/work/libraries/Bridge/src \
               /usr/src/Arduino/build/linux/work/libraries/Esplora/src \
               /usr/src/Arduino/build/linux/work/libraries/Ethernet/src/utility \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/SPI \
               /usr/src/Arduino/build/linux/work/libraries/Ethernet/src \
               /usr/src/Arduino/build/linux/work/libraries/Firmata/src \
               /usr/src/Arduino/build/linux/work/libraries/GSM/src \
               /usr/src/Arduino/build/linux/work/libraries/LiquidCrystal/src \
               /usr/src/Arduino/build/linux/work/libraries/Robot_Control/src \
               /usr/src/Arduino/build/linux/work/libraries/Robot_Control/src/utility \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/Wire \
               /usr/src/Arduino/build/linux/work/libraries/Robot_Motor/src \
               /usr/src/Arduino/build/linux/work/libraries/RobotIRremote/src \
               /usr/src/Arduino/build/linux/work/libraries/Scheduler/src \
               /usr/src/Arduino/build/linux/work/libraries/SD/src \
               /usr/src/Arduino/build/linux/work/libraries/SD/src/utility \
               /usr/src/Arduino/build/linux/work/libraries/SpacebrewYun/src \
               /usr/src/Arduino/build/linux/work/libraries/Stepper/src \
               /usr/src/Arduino/build/linux/work/libraries/Temboo/src \
               /usr/src/Arduino/build/linux/work/libraries/Temboo/src/utility \
               /usr/src/Arduino/build/linux/work/libraries/TFT/src \
               /usr/src/Arduino/build/linux/work/libraries/TFT/src/utility \
               /usr/src/Arduino/build/linux/work/libraries/USBHost/src \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/src/utility \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/src \
               /usr/src/Arduino/build/linux/work/libraries/Servo/src \
               /usr/src/Arduino/build/linux/work/libraries/Servo/src/avr \
               /usr/src/Arduino/build/linux/work/libraries/Servo/src/sam \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/bootloaders/caterina \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/bootloaders/optiboot \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/bootloaders/stk500v2 \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/EEPROM \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/libraries/Wire/utility \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/cores/arduino \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/BOARDS \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/BOARDS/EVK1105 \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/UTILS \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/UTILS/PREPROCESSOR \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/BOARDS/ARDUINO \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/CMSIS/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include/component \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include/instance \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3sd8/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3s/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3s/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include/component \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include/instance \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3u/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3n/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3n/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include/component \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include/instance \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam3xa/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam4s/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ATMEL/sam4s/include/pio \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/libsam \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/libsam/include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/variants/arduino_due_x \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/DRIVERS/PM \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/DRIVERS/FLASHC \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/DRIVERS/GPIO \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/COMPONENTS/MEMORY/DATA_FLASH/AT45DBX \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/CONFIG \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/DRIVERS/INTC \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/UTILS/DEBUG \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/COMPONENTS/WIFI/HD \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/DRIVERS/EIC \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/SERVICES/LWIP/lwip-port-1.3.2/HD/if/include/netif \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/firmwares/atmegaxxu2/arduino-usbdfu \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/firmwares/atmegaxxu2/arduino-usbserial \
               /usr/src/Arduino/build/linux/work/hardware/arduino/avr/firmwares/atmegaxxu2/arduino-usbserial/Lib \
               /usr/src/Arduino/build/linux/work/hardware/tools/avr/avr/include/compat \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Examples/cmsis_example \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/DRIVERS/SPI \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/DRIVERS/USART \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/BOARDS/ARDUINO \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/BOARDS/EVK1105 \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/DRIVERS/PDCA \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/DRIVERS/RTC \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/DRIVERS/TC \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ARM/ARMCM4/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ARM/ARMCM0/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/Device/ARM/ARMCM3/Include \
               /usr/src/Arduino/build/linux/work/hardware/arduino/sam/system/CMSIS/CMSIS/DSP_Lib/Examples/Common/Include \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifi_dnld/src/SOFTWARE_FRAMEWORK/SERVICES/MEMORY/CTRL_ACCESS \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/DRIVERS/EBI/SMC \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/CONFIG \
               /usr/src/Arduino/build/linux/work/libraries/WiFi/extras/wifiHD/src/SOFTWARE_FRAMEWORK/SERVICES/LWIP/lwip-port-1.3.2/HD/if/include/arch

