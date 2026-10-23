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

This is step 1 of three: the simulation, headless.
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

C11 and the C maths library; nothing else.

```sh
make
./syn-biogenesis-3d --seed 42 --frames 51200 --every 10
./syn-biogenesis-3d --size 318 318 317 --import being.bgg --copies 10 --every 10
make test
```

Until the renderer (step 2) exists, two stopgap tools draw the world on the
CPU with the same software rasteriser (`tools/raster.c`):

```sh
make tools/view && ./tools/view --size 318 318 317 --seed 17   # a live window (SDL2)
tools/film.sh first-look.mp4 --size 318 318 317 --seed 17      # a video (ffmpeg)
```

In the viewer:
- drag to turn the camera, use the wheel to zoom;
- space pauses, up/down change the speed;
- `f` follows the life, `n` starts the next seed.

`--help` lists the options. The program prints a CSV row per report:
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

## Credits

The rules are Biogenesis's:
- **Biogenesis**, copyright (c) 2006-2010 Joan Queralt Molina:
  <https://biogenesis.sourceforge.net/>
- **Biogenesis 0.9** by MarcoDBAA (2019).

The code comes by way of [SYN-BIOGENESIS](https://github.com/syn990/SYN-BIOGENESIS),
the C rewrite of 0.9. Each source file names what it was grown from.
`src/sim/dmath.c` holds fdlibm's sine and cosine kernels, copyright (c) 1993
Sun Microsystems, freely distributable with their notice. This project is
not affiliated with the Biogenesis authors.

## Licence

GPL version 2 or later, like Biogenesis (`LICENSE`).
