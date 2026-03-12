# Makefile for LoRaWAN Door Sensor
# Targets: build, upload, clean, monitor

BOARD = esp32:esp32:heltec_wifi_lora_32_V3
PORT = /dev/cu.SLAB_USBtoUART
SKETCH_DIR = .
BAUD = 115200

.PHONY: build upload clean monitor help

help:
	@echo "Available targets:"
	@echo "  make build       - Compile the sketch"
	@echo "  make upload      - Build and upload to device"
	@echo "  make monitor     - Open serial monitor at $(BAUD) baud"
	@echo "  make clean       - Remove build artifacts"

build:
	@echo "Building sketch for $(BOARD)..."
	arduino-cli compile --fqbn $(BOARD) $(SKETCH_DIR)

upload: build
	@echo "Uploading to $(PORT)..."
	arduino-cli upload --port $(PORT) --fqbn $(BOARD) $(SKETCH_DIR)
	@echo "Upload complete!"

monitor:
	@echo "Opening serial monitor on $(PORT) at $(BAUD) baud..."
	@echo "Press Ctrl+C to exit"
	@sleep 1
	@if command -v picocom &> /dev/null; then \
		picocom -b $(BAUD) -l $(PORT); \
	elif command -v minicom &> /dev/null; then \
		minicom -D $(PORT) -b $(BAUD); \
	else \
		screen $(PORT) $(BAUD); \
	fi

clean:
	@echo "Cleaning build artifacts..."
	rm -rf build/
	@echo "Done!"

