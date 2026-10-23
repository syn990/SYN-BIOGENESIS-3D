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

Steps 1 and 2 of three are built, and step 3 is under way: the simulation,
the window it runs in, and the SYN-OS interface over it.
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

C11, SDL2, SDL2_ttf and libepoxy (OpenGL 3.3); cglm comes in `third_party`,
the fonts and SYN-OS themes in `assets`.
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

Over it is the SYN-OS interface, in the SYN-OS theme your desktop has on
(`~/.config/syn-os/current-theme`), or the one last chosen:
- **the bar** (waybar's): the speed, the living, the time, the dead, O2, CO2,
  kills, the frame rate, the theme, and MENU;
- **the card** (SYN-TREK's target card), for a selected organism:
  - its state and figures, and its life and energy as bars;
  - its kind and description, from SYN-BIOGENESIS's species kinds and book
    of words, with whether it is flat or bent;
  - its body, turning;
  - Follow, Kill, Keep and Release;
- **the library** (`l`): SYN-BIOGENESIS's 0.9 beings (Saved, Arena,
  Imported, Forged) and this program's Kept. Each being has its kind,
  description, origin and its body turning. It can be put into the running
  world or used to start a new one. Keep writes a .bgg that 0.9 itself
  still reads;
- **the parameters** (`p`): all of 0.9's, and the box's, with + and -.
  Those marked * take a new world; the rest act at once. In the console,
  `params` and `set NAME VALUE`;
- **the statistics** (`s`): the world's story since genesis, a sample a
  time unit, charted:
  - the living and the dead;
  - the air;
  - the bodies by colour;
  - births, deaths, kills and infections;
  - how many bodies stay flat and how level they lie;
- **the arena** (`a`): two beings from the library fight round after round,
  each round a world of its own seed with ten of each and nothing else,
  run as child processes on every core but the window's. The one whose
  line is alive at the end wins the round. Any round can be watched: the
  window makes that very world, so what you see is the round that was
  scored. `--arena NAME NAME` starts a fight from the command line;
- **menus** (labwc's), from MENU or a right click;
- **notifications** (mako's), and the key hints along the bottom;
- **the console** (a drop-down foot, `` ` ``). It takes commands: `help`,
  `seed N`, `size W H D`, `pitch P`, `import FILE [K]`, `speed N`, `step N`,
  `theme NAME`, `themes`, `select ID`, `genes`, `guide`, `colour NAME`,
  `kind`... `genes` lists the selected organism's genes, with how far
  each one bends. `guide` is SYN-BIOGENESIS's field guide to the colours,
  whose rules are 0.9's here too.

| key or mouse | does |
|---|---|
| drag / right-drag / wheel | turn the camera / move it / zoom |
| click, right-click | select an organism (and follow it), a menu |
| space, `.` | pause, one frame while paused |
| up, down | faster, slower (5 to 960 world frames a second, or as fast as it goes) |
| `f`, `k` | follow the selection or the life, or stop; kill the selection |
| `n`, `r`, home | next seed, restart, the whole box again |
| `t`, shift+`t`, `h` | next theme, previous theme, hide the key hints |
| `l`, `p`, `s`, `a` | the library, the parameters, the statistics, the arena (esc: back to the world) |
| `` ` `` | the console |
| esc | drop the selection, then quit |

`--shot FILE.ppm` draws one picture and quits (with `--seconds S`, after S
seconds). `SDL_VIDEODRIVER=offscreen` runs the window without showing it.
`--help` lists everything.

`--headless` runs the world without a window. It prints a CSV row per
report:
- the air;
- births, deaths, kills, infections and bites;
- the living segment length by colour;
- the share of flat codes and how level the bodies lie;
- with `--import` (up to four beings, each its own line), how many living
  descendants each line has (`marked`, `line2`, `line3`, `line4`).

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
loader and lighting pipeline come from SYN-TREK. The SYN-OS skin and themes,
the field guide, the species kinds and their book of words
(`assets/lexicon`, SYN-TREK's lexicon format) come from SYN-BIOGENESIS,
which follows SYN-DEBT. cglm (`third_party/cglm`)
is copyright (c) 2015 Recep Aslantas, under the MIT licence.
- **Fonts:** in `assets/fonts`, each with its licence file:
  - Noto Sans and SYN Terminal (Terminus, cut down and renamed), under the
    SIL Open Font License;
  - Font Awesome Free 7's solid icons, under the SIL Open Font License. This project is
not affiliated with the Biogenesis authors.

## Licence

GPL version 2 or later, like Biogenesis (`LICENSE`).
