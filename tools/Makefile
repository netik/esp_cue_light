# Cue Light LAN listener (runs on your Mac/Linux PC, not on ESP8266)
#
# macOS: dns_sd.h (DNSServiceBrowse) is in libSystem — no extra -l flag needed.
#        libdns_services is a newer API; this tool uses the classic dns_sd API.
# Linux: sudo apt install libavahi-compat-libdnssd-dev  (provides -ldns_sd)

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

UNAME := $(shell uname)
ifneq ($(UNAME),Darwin)
  LDFLAGS += -ldns_sd
endif

.PHONY: all clean

all: cue_listen

cue_listen: cue_listen.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f cue_listen
