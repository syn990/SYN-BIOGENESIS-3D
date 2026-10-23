# SYN-BIOGENESIS-3D
#
# The simulation is plain C11 and must compute the same world from the same
# seed on every machine: no -ffast-math, and floating-point contraction off
# so the compiler never fuses a multiply and an add into an FMA.

CC ?= cc
CFLAGS ?= -O2 -g
override CFLAGS += -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -ffp-contract=off -Isrc
LDLIBS = -lm

SIM = src/sim/dmath.c src/sim/jrandom.c src/sim/params.c src/sim/genetic_code.c \
      src/sim/organism.c src/sim/world.c src/io/bgg.c
OBJ = $(SIM:src/%.c=obj/%.o)
OBJ_O0 = $(SIM:src/%.c=obj/O0/%.o)

all: syn-biogenesis-3d

syn-biogenesis-3d: $(OBJ) obj/main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

obj/%.o: src/%.c $(wildcard src/sim/*.h src/io/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

# The same program unoptimised: a seed must give the same world either way.
obj/O0/%.o: src/%.c $(wildcard src/sim/*.h src/io/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -O0 -c -o $@ $<

tests/syn-biogenesis-3d-O0: $(OBJ_O0) obj/O0/main.o
	$(CC) $(CFLAGS) -O0 -o $@ $^ $(LDLIBS)

# Until the real renderer exists: a camera that films the headless world
# (tools/film.sh makes the video), and a live window onto it (needs SDL2).
tools/film: tools/film.c tools/raster.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tools/view: tools/view.c tools/raster.c $(OBJ)
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sdl2) -o $@ $^ $(shell pkg-config --libs sdl2) $(LDLIBS)

tests/check: tests/check.c $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: syn-biogenesis-3d tests/check tests/syn-biogenesis-3d-O0
	./tests/check
	./tests/determinism.sh

clean:
	rm -rf obj syn-biogenesis-3d tools/film tools/view tests/check tests/syn-biogenesis-3d-O0 tests/syn-biogenesis-3d-clang

.PHONY: all test clean
