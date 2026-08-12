# Cue Light ESP8266 — build, upload, and deploy helpers
#
# Override on the command line, e.g.:
#   make upload PORT=/dev/cu.usbserial-0002
#   make deploy FQBN=esp8266:esp8266:nodemcuv2:eesz=4M2M,ip=lm2n

ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

ARDUINO_CLI ?= $(shell command -v arduino-cli 2>/dev/null || echo /Users/jna/bin/arduino-cli)
FQBN        ?= esp8266:esp8266:nodemcuv2:eesz=4M2M,ip=lm2n
PORT        ?= /dev/cu.usbserial-0001
BAUD        ?= 115200

CLI_CONFIG  := $(ROOT)arduino-cli.yaml
CLI_FLAGS   := --config-file $(CLI_CONFIG)

SCRIPTS     := $(ROOT)scripts
UPLOAD_FS   := $(SCRIPTS)/upload-littlefs.sh
SETUP_LIBS  := $(SCRIPTS)/setup-libraries.sh

export CUE_LIGHT_PORT := $(PORT)

.PHONY: help all setup compile build upload upload-littlefs littlefs \
        upload-littlefs-fresh littlefs-fresh deploy full-deploy \
        monitor boards list-boards clean

.DEFAULT_GOAL := help

help: ## Show available targets
	@printf '\nCue Light ESP8266\n\n'
	@printf 'Variables (override with VAR=value):\n'
	@printf '  PORT=%s\n' '$(PORT)'
	@printf '  FQBN=%s\n' '$(FQBN)'
	@printf '  BAUD=%s\n' '$(BAUD)'
	@printf '  ARDUINO_CLI=%s\n\n' '$(ARDUINO_CLI)'
	@printf 'Targets:\n'
	@printf '  %-22s %s\n' 'setup' 'Install ESP8266 core and required libraries'
	@printf '  %-22s %s\n' 'compile / build' 'Compile firmware'
	@printf '  %-22s %s\n' 'upload' 'Compile and upload firmware'
	@printf '  %-22s %s\n' 'littlefs' 'Upload data/ to LittleFS (preserves /setup)'
	@printf '  %-22s %s\n' 'littlefs-fresh' 'Upload data/ to LittleFS (wipes WiFi/options)'
	@printf '  %-22s %s\n' 'deploy' 'Upload firmware, then LittleFS'
	@printf '  %-22s %s\n' 'monitor' 'Open serial monitor'
	@printf '  %-22s %s\n' 'boards' 'List connected boards and serial ports'
	@printf '  %-22s %s\n' 'clean' 'Remove local build artifacts'
	@printf '\nExamples:\n'
	@printf '  make compile\n'
	@printf '  make upload PORT=/dev/cu.usbserial-0002\n'
	@printf '  make deploy\n'
	@printf '  make littlefs-fresh\n'

all: compile

setup:
	$(SETUP_LIBS)

compile build:
	$(ARDUINO_CLI) $(CLI_FLAGS) compile --fqbn $(FQBN) $(ROOT)

upload: compile
	$(ARDUINO_CLI) $(CLI_FLAGS) upload --fqbn $(FQBN) --port $(PORT) $(ROOT)

upload-littlefs littlefs:
	$(UPLOAD_FS) --port $(PORT)

upload-littlefs-fresh littlefs-fresh:
	$(UPLOAD_FS) --fresh --port $(PORT)

deploy full-deploy: upload upload-littlefs

monitor:
	$(ARDUINO_CLI) $(CLI_FLAGS) monitor --port $(PORT) --config baudrate=$(BAUD)

boards list-boards:
	$(ARDUINO_CLI) $(CLI_FLAGS) board list

clean:
	rm -rf $(ROOT)build
