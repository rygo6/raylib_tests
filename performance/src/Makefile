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
else
    # macOS: Vulkan headers for the GPU probe come from Homebrew (types only, no linking)
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        ifneq ($(wildcard /opt/homebrew/include/vulkan/vulkan.h),)
            CFLAGS += -I/opt/homebrew/include
        else ifneq ($(wildcard /usr/local/include/vulkan/vulkan.h),)
            CFLAGS += -I/usr/local/include
        endif
    endif
endif

all: performance_capture$(EXT) performance_report$(EXT)

performance_capture$(EXT): performance_capture.c rini.h perf_label.h
	$(CC) -o $@ performance_capture.c $(CFLAGS)

performance_report$(EXT): performance_report.c rini.h perf_label.h
	$(CC) -o $@ performance_report.c $(CFLAGS)

clean:
	rm -f performance_capture$(EXT) performance_report$(EXT)

.PHONY: all clean
