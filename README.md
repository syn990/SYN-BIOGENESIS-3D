# SYN-BIOGENESIS-3D

**Biogenesis in three dimensions: a C11 artificial-life simulation grown
from Biogenesis 0.9's rules.**

Organisms are bodies of coloured segments. Green ones photosynthesise, red
ones eat, white ones infect, gray ones kill, blue ones shield, cyan ones
swim and yellow ones bring more children, all by Biogenesis 0.9's rules.
The difference is that they now live in a box rather than on a plane:
- a gene can bend out of its plane;
- segments are capsules with a thickness;
- every body is a rigid body that tumbles.

A 0.9 being imports as it is and arrives flat; its descendants can learn
depth by mutation.

Steps 1 and 2 of three are built: the simulation, and the window it runs
in.
- [docs/DESIGN.md](docs/DESIGN.md) has the plan, what is kept from 0.9 and
  every change with its reason.
- [docs/sweep](docs/sweep/README.md) asks whether 0.9's life copes with the
  third dimension, over 1,440 runs. It does, as long as the water stays
  crowded:
  - life survives in every shape;
  - hunters fade as the same carbon spreads through deeper water;
  - thin water selects flat bodies;
  - 13 of the 14 0.9 beings that live alone in a flat slab also live in the
    default 3D world.

## Build and run

C11, SDL2 and libepoxy (OpenGL 3.3); cglm comes in `third_party`.
`make NO_WINDOW=1` builds the same program without a window, needing only
the C maths library.

```sh
make
./syn-biogenesis-3d                                   # the default world, 1000 x 1000 x 32
./syn-biogenesis-3d --size 318 318 317 --seed 17 --warm 25
./syn-biogenesis-3d --import being.bgg --copies 10    # a Biogenesis 0.9 being, arriving flat
./syn-biogenesis-3d --headless --seed 42 --frames 51200 --every 10
make test
```

In the window, every segment is a lit capsule. Bites, infections and kills
flash and bloom. The world keeps its own tick, and the picture is drawn
between ticks, so motion stays smooth at any speed.

| key or mouse | does |
|---|---|
| drag / right-drag / wheel | turn the camera / move it / zoom |
| click | select an organism, and follow it |
| space, `.` | pause, one frame while paused |
| up, down | faster, slower (5 to 960 world frames a second, or as fast as it goes) |
| `f` | follow the selection or the life, or stop |
| `n`, `r`, home | next seed, restart, the whole box again |
| esc | drop the selection, then quit |

The title bar shows the seed, the time, the living and the dead, kills, the
speed, the frame rate and the selected organism. `--shot FILE.ppm` draws one
picture and quits; `--help` lists everything.

`--headless` runs the world without a window. It prints a CSV row per
report:
- the air;
- births, deaths, kills, infections and bites;
- the living segment length by colour;
- the share of flat codes and how level the bodies lie;
- with `--import`, how many living descendants the imported being has.

`make test`:
- checks the trigonometry, and that flat codes build 0.9's bodies exactly;
- checks that bent bodies keep their symmetry and mirror images;
- balances the carbon and energy books on every frame of four worlds, from
  a slab one body thick to a cube;
- checks that the optimised, unoptimised and (if installed) clang builds
  reach the same state from the same seed.

`tools/film.sh OUT.mp4 [options]` films a world on the CPU, without a GPU.
`make tools/view` builds the CPU window that came before the renderer, for
machines without OpenGL 3.3.

## Credits

The rules are Biogenesis's:
- **Biogenesis**, copyright (c) 2006-2010 Joan Queralt Molina:
  <https://biogenesis.sourceforge.net/>
- **Biogenesis 0.9** by MarcoDBAA (2019).

The code comes by way of [SYN-BIOGENESIS](https://github.com/syn990/SYN-BIOGENESIS),
the C rewrite of 0.9. Each source file names what it was grown from.
`src/sim/dmath.c` holds fdlibm's sine and cosine kernels, copyright (c) 1993
Sun Microsystems, freely distributable with their notice. The window, shader
loader and lighting pipeline come from SYN-TREK. cglm (`third_party/cglm`)
is copyright (c) 2015 Recep Aslantas, under the MIT licence. This project is
not affiliated with the Biogenesis authors.

## Licence

GPL version 2 or later, like Biogenesis (`LICENSE`).
