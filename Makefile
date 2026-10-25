# SYN-BIOGENESIS-3D
#
# One program: the world in a window, or with --headless as CSV.
#   make                 the program (SDL2, SDL2_ttf, libepoxy; cglm is in third_party)
#   make NO_WINDOW=1     the same program without a window (no SDL2 or GL)
#   make test            the simulation's checks and the determinism check
#   make dist            the program and its assets in dist/syn-biogenesis-3d/
#
# The simulation is plain C11 and must compute the same world from the same
# seed on every machine: no -ffast-math, and floating-point contraction off
# so the compiler never fuses a multiply and an add into an FMA.

CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -ffp-contract=off -Isrc
LDLIBS = -lm
PKG_CONFIG ?= pkg-config

# Windows, with MinGW-w64 (MSYS2's UCRT64): the same program,
# syn-biogenesis-3d.exe, a window program with a manifest (src/win/), as
# SYN-BIOGENESIS builds there. Linked statically: SDL and every library it
# uses are in the program, which needs no DLL but Windows's own, wherever
# it is run from.
ifneq ($(findstring mingw,$(shell $(CC) -dumpmachine)),)
EXE = .exe
# M_PI, which -std=c11 leaves out there
override CFLAGS += -D_USE_MATH_DEFINES
PKG_STATIC = --static
# harfbuzz, under SDL2_ttf, is C++, which its pkg-config file leaves out
CXX_RUNTIME = -lstdc++
LDLIBS += -static
WINDRES ?= windres
WIN_RES = obj/win/syn-biogenesis-3d.res.o
endif

SIM = src/sim/dmath.c src/sim/jrandom.c src/sim/params.c src/sim/genetic_code.c \
      src/sim/organism.c src/sim/world.c src/sim/stats.c src/io/bgg.c src/io/worldfile.c
CORE = src/options.c src/headless.c src/guide/palette.c src/syn/family.c src/syn/record.c \
       src/syn/round.c src/syn/arena.c src/syn/arena_os.c src/syn/history.c src/syn/sidecar.c \
       src/syn/library.c src/syn/telescope.c src/syn/crack.c src/syn/winner.c
WIN = src/app/app.c src/app/interface.c src/app/screens.c src/app/library.c \
      src/app/stats.c src/app/family_view.c src/app/arena.c src/app/image.c src/app/files.c src/app/lab.c src/app/telescope.c src/app/looks.c src/app/minimap.c src/app/flesh.c src/io/png.c \
      src/platform/window.c src/render/shader.c \
      src/render/lighting.c src/render/bodies.c src/render/water.c src/render/lines.c src/ui/gl2d.c \
      src/ui/input.c src/ui/skin.c src/ui/theme.c src/ui/shell.c \
      src/guide/kind.c src/guide/lexicon.c src/guide/guide.c
HEADERS = $(wildcard src/*.h src/sim/*.h src/io/*.h src/app/*.h src/render/*.h src/platform/*.h \
          src/ui/*.h src/guide/*.h src/syn/*.h)

OBJ_SIM = $(SIM:src/%.c=obj/%.o)
OBJ_CORE = $(CORE:src/%.c=obj/%.o)
OBJ_WIN = $(WIN:src/%.c=obj/%.o)

ifeq ($(NO_WINDOW),1)
MAIN_FLAGS = -DSYN3D_NO_WINDOW
PROGRAM_OBJ = $(OBJ_SIM) $(OBJ_CORE) obj/main-nowindow.o
PROGRAM_LIBS = $(LDLIBS)
else
GL_CFLAGS = $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_ttf epoxy) -Ithird_party/cglm/include
GL_LIBS = $(shell $(PKG_CONFIG) $(PKG_STATIC) --libs sdl2 SDL2_ttf epoxy) $(CXX_RUNTIME)
PROGRAM_OBJ = $(OBJ_SIM) $(OBJ_CORE) $(OBJ_WIN) obj/main.o $(WIN_RES)
PROGRAM_LIBS = $(GL_LIBS) $(LDLIBS)
endif

all: syn-biogenesis-3d$(EXE)

syn-biogenesis-3d$(EXE): $(PROGRAM_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(PROGRAM_LIBS)

obj/main-nowindow.o: src/main.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DSYN3D_NO_WINDOW -c -o $@ $<

obj/main.o obj/app/%.o obj/render/%.o obj/platform/%.o obj/ui/%.o: GL_ONLY = $(GL_CFLAGS)

obj/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GL_ONLY) -c -o $@ $<

# Windows: the manifest that runs the program in the UTF-8 code page (so
# the C library's file names are UTF-8, as SDL's text is)
obj/win/syn-biogenesis-3d.res.o: src/win/syn-biogenesis-3d.rc src/win/syn-biogenesis-3d.manifest
	@mkdir -p $(dir $@)
	$(WINDRES) -I src/win -O coff -o $@ $<

# The headless program unoptimised: a seed must give the same world either way.
OBJ_O0 = $(SIM:src/%.c=obj/O0/%.o) $(CORE:src/%.c=obj/O0/%.o) obj/O0/main.o

obj/O0/%.o: src/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O0 -DSYN3D_NO_WINDOW -c -o $@ $<

tests/syn-biogenesis-3d-O0$(EXE): $(OBJ_O0)
	$(CC) $(CFLAGS) -O0 -o $@ $^ $(LDLIBS)

tests/check$(EXE): tests/check.c $(OBJ_SIM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

OBJ_SYN = $(filter obj/syn/%.o obj/guide/palette.o,$(OBJ_CORE))

tests/arena_check$(EXE): tests/arena_check.c $(OBJ_SIM) $(OBJ_SYN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tests/syn_check$(EXE): tests/syn_check.c $(OBJ_SIM) $(OBJ_SYN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tests/telescope_check$(EXE): tests/telescope_check.c $(OBJ_SIM) $(OBJ_SYN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: syn-biogenesis-3d$(EXE) tests/check$(EXE) tests/syn-biogenesis-3d-O0$(EXE) tests/arena_check$(EXE) \
		tests/syn_check$(EXE) tests/telescope_check$(EXE)
	./tests/check
	./tests/syn_check
	./tests/telescope_check
	./tests/determinism.sh
	./tests/arena.sh

# Stopgaps from before the window existed, drawn on the CPU: a camera that
# films the headless world (tools/film.sh makes the video) and a window.
tools/film$(EXE): tools/film.c tools/raster.c $(OBJ_SIM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# The data the telescope's fitness is fitted on (docs/crack).
tools/founders$(EXE): tools/founders.c $(OBJ_SIM)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tools/view$(EXE): tools/view.c tools/raster.c $(OBJ_SIM)
	$(CC) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags sdl2) -o $@ $^ \
		$(shell $(PKG_CONFIG) $(PKG_STATIC) --libs sdl2) $(LDLIBS)

# The program without its debugging information, with its assets, in
# dist/syn-biogenesis-3d/: on Windows a folder that runs anywhere
STRIP ?= strip
dist: syn-biogenesis-3d$(EXE)
	rm -rf dist/syn-biogenesis-3d
	mkdir -p dist/syn-biogenesis-3d
	cp -r assets LICENSE dist/syn-biogenesis-3d/
	$(STRIP) -o dist/syn-biogenesis-3d/syn-biogenesis-3d$(EXE) syn-biogenesis-3d$(EXE)

clean:
	rm -rf obj dist syn-biogenesis-3d$(EXE) tools/film$(EXE) tools/view$(EXE) tools/founders$(EXE) \
		tests/check$(EXE) tests/syn-biogenesis-3d-O0$(EXE) tests/syn-biogenesis-3d-clang$(EXE) \
		tests/arena_check$(EXE) tests/syn_check$(EXE) tests/telescope_check$(EXE)

.PHONY: all test dist clean
