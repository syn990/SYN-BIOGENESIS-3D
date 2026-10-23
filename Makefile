# SYN-BIOGENESIS-3D
#
# One program: the world in a window, or with --headless as CSV.
#   make                 the program (SDL2, SDL2_ttf, libepoxy; cglm is in third_party)
#   make NO_WINDOW=1     the same program without a window (no SDL2 or GL)
#   make test            the simulation's checks and the determinism check
#
# The simulation is plain C11 and must compute the same world from the same
# seed on every machine: no -ffast-math, and floating-point contraction off
# so the compiler never fuses a multiply and an add into an FMA.

CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -ffp-contract=off -Isrc
LDLIBS = -lm
PKG_CONFIG ?= pkg-config

SIM = src/sim/dmath.c src/sim/jrandom.c src/sim/params.c src/sim/genetic_code.c \
      src/sim/organism.c src/sim/world.c src/io/bgg.c
CORE = src/options.c src/headless.c
WIN = src/app/app.c src/app/interface.c src/app/screens.c src/app/library.c \
      src/app/history.c src/app/stats.c src/app/arena.c src/platform/window.c src/render/shader.c \
      src/render/lighting.c src/render/capsules.c src/render/lines.c src/ui/gl2d.c \
      src/ui/input.c src/ui/skin.c src/ui/theme.c src/ui/shell.c src/guide/palette.c \
      src/guide/kind.c src/guide/lexicon.c src/guide/guide.c
HEADERS = $(wildcard src/*.h src/sim/*.h src/io/*.h src/app/*.h src/render/*.h src/platform/*.h \
          src/ui/*.h src/guide/*.h)

OBJ_SIM = $(SIM:src/%.c=obj/%.o)
OBJ_CORE = $(CORE:src/%.c=obj/%.o)
OBJ_WIN = $(WIN:src/%.c=obj/%.o)

ifeq ($(NO_WINDOW),1)
MAIN_FLAGS = -DSYN3D_NO_WINDOW
PROGRAM_OBJ = $(OBJ_SIM) $(OBJ_CORE) obj/main-nowindow.o
PROGRAM_LIBS = $(LDLIBS)
else
GL_CFLAGS = $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf epoxy) -Ithird_party/cglm/include
GL_LIBS = $(shell $(PKG_CONFIG) --libs sdl2 SDL2_ttf epoxy)
PROGRAM_OBJ = $(OBJ_SIM) $(OBJ_CORE) $(OBJ_WIN) obj/main.o
PROGRAM_LIBS = $(GL_LIBS) $(LDLIBS)
endif

all: syn-biogenesis-3d

syn-biogenesis-3d: $(PROGRAM_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(PROGRAM_LIBS)

obj/main-nowindow.o: src/main.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DSYN3D_NO_WINDOW -c -o $@ $<

obj/main.o obj/app/%.o obj/render/%.o obj/platform/%.o obj/ui/%.o: GL_ONLY = $(GL_CFLAGS)

obj/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GL_ONLY) -c -o $@ $<

# The headless program unoptimised: a seed must give the same world either way.
OBJ_O0 = $(SIM:src/%.c=obj/O0/%.o) $(CORE:src/%.c=obj/O0/%.o) obj/O0/main.o

obj/O0/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O0 -DSYN3D_NO_WINDOW -c -o $@ $<

tests/syn-biogenesis-3d-O0: $(OBJ_O0)
	$(CC) $(CFLAGS) -O0 -o $@ $^ $(LDLIBS)

tests/check: tests/check.c $(OBJ_SIM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: syn-biogenesis-3d tests/check tests/syn-biogenesis-3d-O0
	./tests/check
	./tests/determinism.sh

# Stopgaps from before the window existed, drawn on the CPU: a camera that
# films the headless world (tools/film.sh makes the video) and a window.
tools/film: tools/film.c tools/raster.c $(OBJ_SIM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tools/view: tools/view.c tools/raster.c $(OBJ_SIM)
	$(CC) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags sdl2) -o $@ $^ \
		$(shell $(PKG_CONFIG) --libs sdl2) $(LDLIBS)

clean:
	rm -rf obj syn-biogenesis-3d tools/film tools/view tests/check tests/syn-biogenesis-3d-O0 \
		tests/syn-biogenesis-3d-clang

.PHONY: all test clean
