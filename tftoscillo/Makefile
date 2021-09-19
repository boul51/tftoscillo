export BINDIR="$(PWD)/bin"
export LIBDIR="$(PWD)/../lib"
export BINFILE=$(BINDIR)/tftoscillo.ino.bin

export FQBN:="arduino:sam:arduino_due_x"
export DEBUG_CMD:=tail -f

all: build

build:
	arduino-cli compile tftoscillo \
		--verbose \
		--fqbn arduino:sam:arduino_due_x \
		--warnings all \
		--build-property 'compiler.cpp.extra_flags=-DSERIALCOMMAND_HARDWAREONLY' \
		--library "$(LIBDIR)/AdcDma" \
		--library "$(LIBDIR)/ArduinoSerialCommand" \
		--library "$(LIBDIR)/GenSigDma" \
		--library "$(LIBDIR)/LibDbg" \
		--library "$(LIBDIR)/TFT" \
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
	$(DEBUG_CMD) $$SERIALPORT

clean:
	rm -rf /tmp/arduino-sketch-*
	rm -rf $(BINDIR)
