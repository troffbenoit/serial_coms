###############################################################################
# FILE:
#     Makefile
#
# DATE:
#     2026-07-08
#
# PROJECT:
#     Serial Communications Utilities
#
# PURPOSE:
#     Build serial terminal applications from src/ into bin/.
###############################################################################

CC = clang

CFLAGS = -Wall -Wextra -pedantic

SRC_DIR = src
BIN_DIR = bin
BUILD_DIR = build

STANDARD_SOURCE = $(SRC_DIR)/serial_terminal.c
NCURSES_SOURCE = $(SRC_DIR)/serial_terminal_ncurses.c

ARM64_OUTPUT = $(BIN_DIR)/serial_terminal_arm64
INTEL_OUTPUT = $(BIN_DIR)/serial_terminal_x86_64
NCURSES_ARM64_OUTPUT = $(BIN_DIR)/serial_terminal_ncurses_arm64
NCURSES_INTEL_OUTPUT = $(BIN_DIR)/serial_terminal_ncurses_x86_64

all: dirs arm64 intel ncurses_arm64 ncurses_intel

dirs:
	@mkdir -p $(BIN_DIR)
	@mkdir -p $(BUILD_DIR)

arm64: dirs
	$(CC) -arch arm64 $(CFLAGS) -o $(ARM64_OUTPUT) $(STANDARD_SOURCE)

intel: dirs
	$(CC) -arch x86_64 $(CFLAGS) -o $(INTEL_OUTPUT) $(STANDARD_SOURCE)

ncurses_arm64: dirs
	$(CC) -arch arm64 $(CFLAGS) -o $(NCURSES_ARM64_OUTPUT) $(NCURSES_SOURCE) -lncurses

ncurses_intel: dirs
	$(CC) -arch x86_64 $(CFLAGS) -o $(NCURSES_INTEL_OUTPUT) $(NCURSES_SOURCE) -lncurses

clean:
	rm -f $(ARM64_OUTPUT)
	rm -f $(INTEL_OUTPUT)
	rm -f $(NCURSES_ARM64_OUTPUT)
	rm -f $(NCURSES_INTEL_OUTPUT)
	rm -rf $(BUILD_DIR)/*

help:
	@echo ""
	@echo "Serial Communications Build Targets"
	@echo ""
	@echo "  make              Build all targets"
	@echo "  make arm64        Build Apple Silicon terminal"
	@echo "  make intel        Build Intel Mac terminal"
	@echo "  make ncurses_arm64"
	@echo "  make ncurses_intel"
	@echo "  make clean        Remove generated build files"
	@echo ""
