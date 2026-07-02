# Build the raylib image-comparison tools (C99, link raylib).
#
#   make            build image_comparison_capture and image_comparison_diff
#   make clean      remove the built tools
#
# Assumes the raylib repo is a sibling (../../raylib) built at ../../raylib/src/libraylib.a.
# Override RAYLIB_SRC if raylib lives elsewhere.

CC        ?= gcc
RAYLIB_SRC ?= ../../raylib/src
CFLAGS     = -std=c99 -Wall -O2 -pipe -I$(RAYLIB_SRC)
LDFLAGS    = -L$(RAYLIB_SRC)

ifeq ($(OS),Windows_NT)
    EXT    = .exe
    LDLIBS = -lraylib -lopengl32 -lgdi32 -lwinmm
else
    EXT    =
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        LDLIBS = -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -framework CoreAudio
    else
        LDLIBS = -lraylib -lGL -lm -lpthread -ldl -lrt -lX11
    endif
endif

TOOLS = image_comparison_capture$(EXT) image_comparison_diff$(EXT)

all: $(TOOLS)

image_comparison_capture$(EXT): image_comparison_capture.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LDLIBS)

image_comparison_diff$(EXT): image_comparison_diff.c
	$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TOOLS)

.PHONY: all clean
