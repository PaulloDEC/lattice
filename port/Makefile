# Linux:   make            (needs libsdl2-dev)
# Windows: make CC=x86_64-w64-mingw32-gcc SDL_CONFIG=<path to mingw sdl2-config>
CC ?= gcc
SDL_CONFIG ?= sdl2-config
SRC := $(filter-out src/main_sdl.c,$(wildcard src/*.c))
CFLAGS ?= -O2 -std=c11 -Wall
hocus: $(SRC) src/main_sdl.c
	$(CC) $(CFLAGS) $(shell $(SDL_CONFIG) --cflags) -o $@ $^ $(shell $(SDL_CONFIG) --libs) -lm
clean:
	rm -f hocus
