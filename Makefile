# Cue Light ESP8266 — build, upload, and deploy helpers
#
# Two default boards (override PORT= for ad-hoc uploads):
#   board1 / 0001  →  /dev/cu.usbserial-0001
#   board2 / 83430 →  /dev/cu.usbserial-83430
#
# Examples:
#   make upload-board1
#   make deploy-board2
#   make deploy-both
#   make upload PORT=/dev/cu.usbserial-0001

ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

ARDUINO_CLI ?= $(shell command -v arduino-cli 2>/dev/null || echo /Users/jna/bin/arduino-cli)
FQBN        ?= esp8266:esp8266:nodemcuv2:eesz=4M2M,ip=lm2n
BAUD        ?= 115200

BOARD1_PORT ?= /dev/cu.usbserial-0001
BOARD2_PORT ?= /dev/cu.usbserial-83430
PORT        ?= $(BOARD1_PORT)

CLI_CONFIG  := $(ROOT)arduino-cli.yaml
CLI_FLAGS   := --config-file $(CLI_CONFIG)

SCRIPTS     := $(ROOT)scripts
UPLOAD_FS   := $(SCRIPTS)/upload-littlefs.sh
SETUP_LIBS  := $(SCRIPTS)/setup-libraries.sh

.PHONY: help all setup compile build \
        upload upload-board1 upload-board2 upload-both upload-0001 upload-83430 \
        littlefs littlefs-board1 littlefs-board2 littlefs-both \
        littlefs-fresh littlefs-fresh-board1 littlefs-fresh-board2 littlefs-fresh-both \
        deploy deploy-board1 deploy-board2 deploy-both \
        monitor monitor-board1 monitor-board2 \
        boards list-boards clean

.DEFAULT_GOAL := help

help: ## Show available targets
	@printf '\nCue Light ESP8266\n\n'
	@printf 'Boards:\n'
	@printf '  board1 (0001)  PORT=%s\n' '$(BOARD1_PORT)'
	@printf '  board2 (83430) PORT=%s\n\n' '$(BOARD2_PORT)'
	@printf 'Variables (override with VAR=value):\n'
	@printf '  FQBN=%s\n' '$(FQBN)'
	@printf '  BAUD=%s\n' '$(BAUD)'
	@printf '  ARDUINO_CLI=%s\n\n' '$(ARDUINO_CLI)'
	@printf 'Shared targets:\n'
	@printf '  %-24s %s\n' 'setup' 'Install ESP8266 core and required libraries'
	@printf '  %-24s %s\n' 'compile / build' 'Compile firmware once'
	@printf '  %-24s %s\n' 'boards' 'List connected boards and serial ports'
	@printf '  %-24s %s\n' 'clean' 'Remove local build artifacts'
	@printf '\nPer-board targets (firmware):\n'
	@printf '  %-24s %s\n' 'upload-board1' 'Compile + upload to board 0001'
	@printf '  %-24s %s\n' 'upload-board2' 'Compile + upload to board 83430'
	@printf '  %-24s %s\n' 'upload-both' 'Compile + upload to both boards'
	@printf '\nPer-board targets (LittleFS):\n'
	@printf '  %-24s %s\n' 'littlefs-board1' 'Upload data/ to board 0001'
	@printf '  %-24s %s\n' 'littlefs-board2' 'Upload data/ to board 83430'
	@printf '  %-24s %s\n' 'littlefs-both' 'Upload data/ to both boards'
	@printf '  %-24s %s\n' 'littlefs-fresh-board1' 'Wipe + upload data/ on board 0001'
	@printf '  %-24s %s\n' 'littlefs-fresh-board2' 'Wipe + upload data/ on board 83430'
	@printf '\nFull deploy (firmware + LittleFS):\n'
	@printf '  %-24s %s\n' 'deploy-board1' 'Full deploy to board 0001'
	@printf '  %-24s %s\n' 'deploy-board2' 'Full deploy to board 83430'
	@printf '  %-24s %s\n' 'deploy-both' 'Full deploy to both boards'
	@printf '\nSerial monitor:\n'
	@printf '  %-24s %s\n' 'monitor-board1' 'Monitor board 0001'
	@printf '  %-24s %s\n' 'monitor-board2' 'Monitor board 83430'
	@printf '\nLegacy aliases:\n'
	@printf '  upload / littlefs / deploy / monitor use PORT=%s\n\n' '$(PORT)'

all: compile

setup:
	$(SETUP_LIBS)

compile build:
	$(ARDUINO_CLI) $(CLI_FLAGS) compile --fqbn $(FQBN) $(ROOT)

upload upload-board1 upload-0001: compile
	$(ARDUINO_CLI) $(CLI_FLAGS) upload --fqbn $(FQBN) --port $(BOARD1_PORT) $(ROOT)

upload-board2 upload-83430: compile
	$(ARDUINO_CLI) $(CLI_FLAGS) upload --fqbn $(FQBN) --port $(BOARD2_PORT) $(ROOT)

upload-both: compile
	$(ARDUINO_CLI) $(CLI_FLAGS) upload --fqbn $(FQBN) --port $(BOARD1_PORT) $(ROOT)
	$(ARDUINO_CLI) $(CLI_FLAGS) upload --fqbn $(FQBN) --port $(BOARD2_PORT) $(ROOT)

littlefs littlefs-board1:
	CUE_LIGHT_PORT=$(BOARD1_PORT) $(UPLOAD_FS) --port $(BOARD1_PORT)

littlefs-board2:
	CUE_LIGHT_PORT=$(BOARD2_PORT) $(UPLOAD_FS) --port $(BOARD2_PORT)

littlefs-both: littlefs-board1 littlefs-board2

littlefs-fresh littlefs-fresh-board1:
	CUE_LIGHT_PORT=$(BOARD1_PORT) $(UPLOAD_FS) --fresh --port $(BOARD1_PORT)

littlefs-fresh-board2:
	CUE_LIGHT_PORT=$(BOARD2_PORT) $(UPLOAD_FS) --fresh --port $(BOARD2_PORT)

littlefs-fresh-both: littlefs-fresh-board1 littlefs-fresh-board2

deploy deploy-board1: upload-board1 littlefs-board1

deploy-board2: upload-board2 littlefs-board2

deploy-both: upload-both littlefs-both

monitor monitor-board1:
	$(ARDUINO_CLI) $(CLI_FLAGS) monitor --port $(BOARD1_PORT) --config baudrate=$(BAUD)

monitor-board2:
	$(ARDUINO_CLI) $(CLI_FLAGS) monitor --port $(BOARD2_PORT) --config baudrate=$(BAUD)

boards list-boards:
	$(ARDUINO_CLI) $(CLI_FLAGS) board list

clean:
	rm -rf $(ROOT)build
