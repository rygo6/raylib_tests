# raylib [tests] - performance harness tools
#
# Builds the two standalone tools (no raylib dependency):
#   performance_capture  - runs each example RUNS times at full speed, collects run_<n>.rini
#   performance_report   - aggregates captures into per-backend + comparison HTML reports
#
# Windows / MinGW-w64. DXGI and psapi are resolved dynamically at run time, so no extra
# import libraries are needed here.

ifeq ($(origin CC),default)
    CC = gcc
endif
CFLAGS  = -O2 -std=c99 -Wall -Wno-unused-result -D_DEFAULT_SOURCE
EXT     =

ifeq ($(OS),Windows_NT)
    EXT = .exe
endif

all: performance_capture$(EXT) performance_report$(EXT)

performance_capture$(EXT): performance_capture.c rini.h
	$(CC) -o $@ performance_capture.c $(CFLAGS)

performance_report$(EXT): performance_report.c rini.h
	$(CC) -o $@ performance_report.c $(CFLAGS)

clean:
	rm -f performance_capture$(EXT) performance_report$(EXT)

.PHONY: all clean
