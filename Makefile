CC ?= cc
CFLAGS = -Wall -Wextra -pedantic

BIN_DIR = bin
BUILD_DIR = build

STANDARD_SOURCE = src/serial_terminal.c
NCURSES_SOURCE = src/serial_terminal_ncurses.c

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)

HOST_OUTPUT = $(BIN_DIR)/serial_terminal_$(UNAME_S)_$(UNAME_M)
HOST_NCURSES_OUTPUT = $(BIN_DIR)/serial_terminal_ncurses_$(UNAME_S)_$(UNAME_M)

MAC_ARM64_OUTPUT = $(BIN_DIR)/serial_terminal_mac_arm64
MAC_INTEL_OUTPUT = $(BIN_DIR)/serial_terminal_mac_x86_64
MAC_NCURSES_ARM64_OUTPUT = $(BIN_DIR)/serial_terminal_ncurses_mac_arm64
MAC_NCURSES_INTEL_OUTPUT = $(BIN_DIR)/serial_terminal_ncurses_mac_x86_64

all: host

dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(BUILD_DIR)

host: dirs standard ncurses

standard: dirs
	$(CC) $(CFLAGS) -o $(HOST_OUTPUT) $(STANDARD_SOURCE)

ncurses: dirs
	$(CC) $(CFLAGS) -o $(HOST_NCURSES_OUTPUT) $(NCURSES_SOURCE) -lncurses

mac_all: dirs mac_arm64 mac_intel mac_ncurses_arm64 mac_ncurses_intel

mac_arm64: dirs
	$(CC) -arch arm64 $(CFLAGS) -o $(MAC_ARM64_OUTPUT) $(STANDARD_SOURCE)

mac_intel: dirs
	$(CC) -arch x86_64 $(CFLAGS) -o $(MAC_INTEL_OUTPUT) $(STANDARD_SOURCE)

mac_ncurses_arm64: dirs
	$(CC) -arch arm64 $(CFLAGS) -o $(MAC_NCURSES_ARM64_OUTPUT) $(NCURSES_SOURCE) -lncurses

mac_ncurses_intel: dirs
	$(CC) -arch x86_64 $(CFLAGS) -o $(MAC_NCURSES_INTEL_OUTPUT) $(NCURSES_SOURCE) -lncurses

clean:
	rm -f $(BIN_DIR)/serial_terminal*
	rm -rf $(BUILD_DIR)/*

help:
	@echo ""
	@echo "Serial Communications Build Targets"
	@echo ""
	@echo "  make              Build for current machine"
	@echo "  make host         Build standard and ncurses for current machine"
	@echo "  make standard     Build standard version for current machine"
	@echo "  make ncurses      Build ncurses version for current machine"
	@echo "  make mac_all      Build Intel + ARM Mac binaries"
	@echo "  make mac_arm64"
	@echo "  make mac_intel"
	@echo "  make clean"
	@echo ""
