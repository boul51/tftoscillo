export BINDIR="$(CURDIR)/bin"
export LIBDIR="$(CURDIR)/../lib"
export BINFILE=$(BINDIR)/tftoscillo.ino.bin

export FQBN ?= "arduino:sam:arduino_due_x"
export DEBUG_CMD ?= minicom -D
export HAS_LOGGER ?= 1

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
		--build-property 'compiler.cpp.extra_flags=-Wall -DNO_PRAGMA_MARK -DHAS_LOGGER=$(HAS_LOGGER) -I$(CURDIR) -DSERIALCOMMAND_HARDWAREONLY -DSERIAL_IFACE=$(SERIAL_IFACE)' \
		--library "$(LIBDIR)/AdcDma" \
		--library "$(LIBDIR)/ArduinoSerialCommand" \
		--library "$(LIBDIR)/GenSigDma" \
		--library "$(LIBDIR)/TFT" \
		--library "$(LIBDIR)/Arduino-PrintStream" \
		--library "$(LIBDIR)/MemoryFree" \
		--library "$(LIBDIR)/OscDisplay" \
		--library "$(LIBDIR)/OscDisplayDriver" \
		--library "$(LIBDIR)/arduino-logger" \
		--library "$(LIBDIR)/arduino-printf" \
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
	sleep 1; \
	echo "Waiting for serial port to settle"; \
	while [ ! -e $$SERIALPORT ]; do sleep 0.1; done; \
	echo "Serial port is ready"; \
	stty raw -F $$SERIALPORT -echo; \
	$(DEBUG_CMD) $$SERIALPORT

clean:
	rm -rf /tmp/arduino-sketch-*
	rm -rf $(BINDIR)
