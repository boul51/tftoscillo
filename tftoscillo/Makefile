export BINDIR="$(PWD)/bin"
export LIBDIR="$(PWD)/../lib"
export BINFILE=$(BINDIR)/tftoscillo.ino.bin

export FQBN ?= "arduino:sam:arduino_due_x"
export DEBUG_CMD ?= minicom -D

# Define SERIAL_IFACE to:
# - Serial to use programming port (near the power supply connector)
# - SerialUSB to use native port (near the reset button)
# Native port is faster to upload the program, but seems to get unavailable after a program crash

export SERIAL_IFACE ?= SerialUSB

all: build

build:
	arduino-cli compile tftoscillo \
		--verbose \
		--fqbn $(FQBN) \
		--warnings all \
		--build-property 'compiler.cpp.extra_flags=-DSERIALCOMMAND_HARDWAREONLY -DSERIAL_IFACE=$(SERIAL_IFACE)' \
		--library "$(LIBDIR)/AdcDma" \
		--library "$(LIBDIR)/ArduinoSerialCommand" \
		--library "$(LIBDIR)/GenSigDma" \
		--library "$(LIBDIR)/LibDbg" \
		--library "$(LIBDIR)/TFT" \
		--library "$(LIBDIR)/Arduino-PrintStream" \
		--output-dir $(BINDIR)

run:
	@echo "Searching board type $(FQBN)"; \
	SERIALPORT=`arduino-cli board list | grep "$(FQBN)" | awk '{print $$1}'`; \
	if [ -z $$SERIALPORT ]; then \
		echo "No serial port found"; \
		exit 1; \
	fi; \
	echo "Found board $(FQBN) on port $$SERIALPORT"; \
	echo "Killing apps holding serial port"; \
	for pid in `lsof -t $$SERIALPORT`; do \
		echo killing PID "$$pid"; \
		kill -9 $$pid; \
	done; \
	echo "Starting upload"; \
	arduino-cli upload --fqbn $(FQBN) --port $$SERIALPORT --input-file $(BINFILE); \
	echo "Starting command $(DEBUG_CMD) $$SERIALPORT, use DEBUG_CMD env variable to override"; \
	sleep 0.5; \
	$(DEBUG_CMD) $$SERIALPORT

clean:
	rm -rf /tmp/arduino-sketch-*
	rm -rf $(BINDIR)
