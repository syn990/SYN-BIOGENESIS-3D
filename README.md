# SYN-BIOGENESIS-3D

**Biogenesis in three dimensions: a C11 artificial-life simulation grown
from Biogenesis 0.9 into a box of water.**

Organisms are bodies of coloured segments. Green ones photosynthesise, red
ones eat, white ones infect, gray ones kill, blue ones shield, cyan ones
swim and yellow ones bring more children, as in Biogenesis 0.9; purple
ones, from Color Mod 2.01, live on methane. They live in a box of water
rather than on a plane, and the water has a top and a bottom:
- light falls from the surface, dims with depth and in the shade of the
  bodies above, and a leaf catches the light it lies across;
- CO2 is dissolved in the water, cell by cell, and spreads slowly, so a
  crowded bed of plants starves and whatever breathes beside it feeds it;
- the dead sink, and rot into methane where they come to rest, so the deep
  water holds what purple segments live on, whatever the light; the
  methane turns slowly back into CO2;
- every line keeps to a height of its own, a gene from the floor to the
  surface: its cilia swim it there and its float takes a body without
  them;
- cilia swim steadily, turning the body to face where it is going: up or
  down to its height, and across as its inherited drives pull it (out of
  the shade, to plants, away from hunters...); every organism sees, by the
  light, and smells the dead;
- a body's arms stand as a disk (0.9's), a cone (tilted out of the disk)
  or an orb (spread over a sphere); a gene can bend out of its plane;
  segments are capsules; every body is a rigid body that tumbles, and
  leaves turn their body to lie level.

Every Biogenesis's beings come in: a 0.9 being as it was, flat, with its
height and drives read from its body (its leaves high and seeking the
light, its teeth seeking prey); a Color Mod 2.01 being with each of its
colours given the one here that does that job, its branches straightened
and its reaction genes left behind; one from an earlier version of this
program keeping to the height its light drive liked. Nothing is refused
for lacking what this program has.

Days are an option (`set day_length 8`): life then sinks with the morning
and rises with the dusk, but the worlds are poorer for it (see
[the water](docs/DESIGN.md#the-water)), so they are off by default.

The simulation, the window, the SYN-OS interface over it and the water are
built.
- [docs/DESIGN.md](docs/DESIGN.md) has the plan, what is kept from 0.9 and
  every change with its reason.
- [docs/ecology](docs/ecology/README.md) measures who wins in the water,
  against the first rules (0.9's in a box), over 1,992 runs.
- [docs/sweep](docs/sweep/README.md) is the first rules' study: 0.9's life
  coped with a third dimension only where the water was crowded, and plants
  owned the worlds. It is why the rules changed.

## Build and run

C11, SDL2, SDL2_ttf and libepoxy (OpenGL 3.3); cglm comes in `third_party`,
the fonts and SYN-OS themes in `assets`.
`make NO_WINDOW=1` builds the same program without a window, needing only
the C maths library.

```sh
make
./syn-biogenesis-3d                                   # the default world, 600 x 600 x 300
./syn-biogenesis-3d --size 1000 1000 64 --seed 17 --warm 100   # a pond, 100 time units on
./syn-biogenesis-3d --import being.bgg --copies 10    # any Biogenesis's being (0.9's arrive flat)
./syn-biogenesis-3d --headless --seed 42 --frames 51200 --every 10
make test
```

In the window, bodies are drawn as flesh, each colour its own tissue:
green a veined leaf blade facing the body's up, red a claw tapering to a
hooked point, cyan a slender whip that beats, blue armour plates, white
spotted with spores, gray ridged bone, yellow a sac of eggs, purple a
bladder with bubbles rising in it, a corpse shrivelled; each body thickest
at its hub, where a cell sits, a membrane round a nucleus. Light shows
through thin flesh, and everything is as bright as the water's light where
it is: dim deep down and in the shade. Bites, infections and kills flash
and bloom. The world keeps its own tick, and the picture is drawn between
ticks, so motion stays smooth at any speed. How a body looks is only that:
in the world its segments still touch as capsules `segment_radius` thick.
Detail falls with distance: a body near the eye gets the fine mesh, one
further off a coarse one, and one far away is drawn as lines; what is out
of view is not drawn. The Classic look draws every body as plain lines,
still, as 0.9 did: the fastest way to watch.

Over it is the SYN-OS interface, in the SYN-OS theme your desktop has on
(`~/.config/syn-os/current-theme`), or the one last chosen:
- **the bar** (waybar's): the speed, the living, the time, the dead, O2, CO2,
  kills, the frame rate, the theme, and MENU;
- **the card** (SYN-TREK's target card), for a selected organism:
  - its state and figures, and its life and energy as bars;
  - its depth and the light it has;
  - its kind and description, from SYN-BIOGENESIS's species kinds and book
    of words, with whether it is flat or bent and where its cilia steer it;
  - its body, turning;
  - Feed, Weaken, Kill, Copy, Track, Genes, Family and Keep;
- **the laboratory** (LABORATORY on the bar, `G` for the organism
  selected, or a being's menu: Open in the laboratory), SYN-BIOGENESIS's
  Genetic Laboratory:
  - each gene's length, turn and colour as 0.9's laboratory has them, and
    its bend, out of the flat; typed (click the number) or with - and +;
    genes added, inserted and deleted; the colours with the field guide's
    line on each;
  - symmetry, mirror and dispersed children, and how its arms stand
    (Disk, Cone with its tilt, Orb); the segments, the energy to
    reproduce, the life expectancy, its kind, and whether it is flat;
  - the height its line keeps to and its drives (light, carbon, plants,
    hunters, the dead, company), or those its body asks for (From its
    body), or none;
  - its body as it grows, turning, dragged round and tilted to see the
    bends;
  - Copy to clipboard (then Paste in the world), Cancel (esc), Clear,
    Import... a .bgg, and Keep in the library;
  - the code is kept while other screens are looked at, until it is copied
    or cancelled;
- **the library**, SYN-BIOGENESIS's being library
  (`~/.config/syn-biogenesis-3d/beings`):
  - folders, nested as deep as wanted (Saved, Imported, Arena and the Bin
    to begin with), made, renamed and removed; a .bgg put in one by hand
    is in the library too;
  - each being drawn, with where it came from and when it was kept; it can
    be renamed, moved, deleted to the Bin (and from the Bin for good, on a
    second click), put on the arena's ladder, put into the world, opened in
    the laboratory (or a double click), copied, or used to start a new
    world with ten of it among the founders;
  - IMPORT .BGG copies a file in; Keep on the card, in the laboratory and
    on any being shown puts one in (Saved, or Arena for the arena's);
  - SYN-BIOGENESIS's own libraries, of 0.9 beings and of Color Mod's
    (`~/.config/syn-biogenesis/beings/0.9` and `.../colour`), and the
    arena's ladder are shown beside it, read only, Color Mod's beings
    converted as they are read: any of theirs can be kept in this one,
    and a being imported from anywhere is kept as this program's, with
    what was converted written in its note;
  - the FIELD GUIDE: what each colour does in the water, and the kinds of
    being;
- **the parameters**: all of 0.9's, and the water's, typed (click the
  number; Enter or Tab takes it, Escape puts it back) or with + and -.
  Those marked * take a new world; the rest act at once. In the console,
  `params` and `set NAME VALUE`;
- **the statistics**: SYN-BIOGENESIS's screen (`src/syn/history.c`, the
  screen `src/app/stats.c`), five tabs, 1 to 5:
  - OVERVIEW: the world now, 0.9's averages and records (its
    WorldStatistics, now kept by the world and in its file), the colours of
    the living, the champions alive and the record holders;
  - CHARTS: 0.9's twelve measures over the whole run, its last quarter or
    last 5%, then the water's own: kills and bites, how deep the leaves and
    the hunters live, how many bodies are flat and how level they lie;
  - EPOCHS: the run cut into epochs, each with its colour, its best species
    and its champions;
  - FAMILY: the family tree, live, its species named as SYN-BIOGENESIS
    names them (VELORAX-3), with EXPORT (Graphviz's `family.dot`, every
    species' picture and code, the best of each epoch);
  - TIMELINE: the lines' river and what happened (records, extinctions,
    colours and lines rising and dying, the air, what you did);
  - saved with the world beside it (`world.b3w.syn`), so an opened world's
    history goes on; the organism card names its species, and Family opens
    it in the tree;
  - a being shown opens in the laboratory on a click (a species in the
    tree on a double click); a right click copies it or keeps it;
- **the arena**: SYN-BIOGENESIS's arena, a ladder that winners climb,
  its rounds run as fast as they go on several cores at once:
  - a qualifier is a world of brand-new beings, and the bloodline that wins
    it goes on the ladder at level 1; from then on beings of a level share
    a world (duels, four-way or eight-way), the winner goes up a level and
    the rest are knocked out;
  - its species and bloodlines are named as SYN-BIOGENESIS names them
    (VELORAX-3, the VELORAX line), from a family tree run beside each round;
  - SETUP: the world (the default box, four or nine times its surface, a
    pond, a cube, or the parameters' own), the round's length, sudden
    death, how many rounds, how many cores, fighters a round, newcomers;
    with the leaderboard, the ladder, the reigning champion and the new
    beings;
  - BATTLES: each round running, the bloodlines' shares charted as it goes;
    the ladder and the climb;
  - TREE: every being joined to the one that knocked it out;
  - RECORD: every round ever fought, kept for good, with its bloodline's
    genesis and final beings and what they add up to;
  - any round running or recorded can be watched (WATCH): the window makes
    its world again from its parameters, seed and fighters, the very round
    frame for frame (`make test` replays them against the workers' worlds);
  - a being shown opens in the laboratory on a click; a right click can
    also copy it or keep it in the library's Arena folder; the library's
    To the arena puts one on the ladder at level 1;
  - the ladder and record are kept in the configuration folder
    (`arena.board`, `arena.record`). Without a window: `syn-biogenesis-3d
    --arena` (`--arena --help`), as SYN-BIOGENESIS's;
- **the telescope** (TELESCOPE on the bar, or the main menu),
  SYN-BIOGENESIS's dice telescope worked out again for this program's
  genesis (`src/syn/telescope.c`, `src/syn/crack.c`):
  - A WORLD'S SEED: the seed a saved world (or this one, as it is) was made
    from, found from the genes its founders drew at frame 0 and proved by
    replaying that seed to the saved frame with the save's own parameters;
    opened, the world then knows its seed, so Restart begins it again;
  - WHERE A BEING LIVES (Where on the card, Where it lives in the menus,
    the library and the arena's record): its genesis ancestor drawn again
    from its genes, every seed whose genesis makes that ancestor, and the
    being's later draws placed in the ancestor's stream (lengths and turns
    as in 0.9, and here bends and drives too), so the one seed it lives
    in is found by running the seeds and checking each draw as the stream
    passes it; the arena's recorded rounds that drew its genes; each scene
    (where a gene was drawn, where it is alive, its bloodline alone, gone)
    opens that world at that frame;
  - FORGE A WINNER: many seeds' founders made without running their
    worlds and weighed by a fit of which founder's bloodline leads a world
    (`docs/crack`), the likeliest seeds run, and the being their leading
    bloodline leads as kept, put on the arena's ladder or looked up;
  - without a window: `syn-biogenesis-3d --telescope` (`jump`, `seed`,
    `gene`, `world`, `find`, `crack`, `forge`, `basins`, `scene`);
- **Create a winner** (the command list's NEW LIFE, or the water's menu):
  a being worked out for the world as it is, at the place asked for. The
  world waits while a copy of it is searched on every core: its commonest
  codes, their mutations, the library's beings and the fittest of many
  random codes, each pasted there and at the most open places in the
  water and run on exactly as the world will go, the best bred on while
  time is left. The winner is pasted where it was worked out, and when
  the calculated time comes the window says whether the world went as
  calculated (it does, unless something else is done to it). Without a
  window: `syn-biogenesis-3d --winner WORLD.b3w`;
- **the looks** (`L`, or the command list's VIEW), SYN-BIOGENESIS's four
  drawn in 3D: Classic (0.9's colours in the theme's water, every body
  plain lines, nothing moving), Biological
  (living colours and every tissue's pattern in green-blue water: a
  sediment floor with the caustics' web moving on it and each body's
  shadow, shafts of light from a rippling surface, motes drifting down),
  Galaxy (glowing bodies in the dark among stars) and Digital (SYN-OS's
  colours in a box of red grids); every look keeps what the colours mean,
  and the choice is kept with the theme. Every being shown on a screen
  (the laboratory, the library, statistics, the arena, the telescope) and
  its saved image are drawn the same way;
- **the minimap** (`M`, or OPTIONS), SYN-BIOGENESIS's, in two panes: the
  box from above and from the side (its depth), every organism a dot in
  the look's colour of what most of it is, where the camera looks and from
  which way; a click or a drag moves the camera there. It shows while part
  of the box is out of view;
- **the main menu** over the living world, as SYN-BIOGENESIS opens on:
  CONTINUE, NEW WORLD, LOAD WORLD, OPTIONS, ARENA, LIBRARY, TELESCOPE,
  QUIT (MENU on the bar, or esc with nothing selected); **OPTIONS** has
  fullscreen, the look, the minimap and every SYN-OS theme by family;
- **the commands** (`/`, or the bar's glyph): SYN-BIOGENESIS's list, wmenu's
  way, of everything the window does under SYN-OS's root-menu headings
  (WORLD, THE WATER, EVERYONE, NEW LIFE, THE ORGANISM SELECTED, VIEW,
  SCREENS, SYSTEM); typing filters it. Among them 0.9's own: new world,
  open, save as, CO2 up and down (spread through the water), kill all,
  disperse all corpses, a random organism, paste, import a .bgg; for the
  organism selected feed, weaken, force reproduction, rejuvenate, kill,
  copy, export its genes, save its image, keep, revive and disperse a
  corpse;
- **the file browser** (SYN-BIOGENESIS's three columns: the folder above,
  this one with sizes and dates, and a preview that draws a .bgg's being),
  for worlds, beings and pictures;
- **menus** (labwc's), from a right click: on an organism its actions, on
  open water a new organism there (paste, a random one, an import), where
  the pointer's ray crosses the middle of the view;
- **notifications** (mako's), and the key hints along the bottom;
- **the console** (a drop-down foot, `` ` ``). It takes every command by its
  word (`feed`, `killall`, `co2+`; `help` lists them), and its own: `help`,
  `info`, `get NAME`, `find ID`, `run N`, `co2 + N`,
  `seed N`, `size W H D`, `pitch P`, `import FILE [K]`, `speed N`, `step N`,
  `goto T`, `theme NAME`, `themes`, `select ID`, `genes`, `guide`, `colour NAME`,
  `kind`... `genes` lists the selected organism's genes, with how far
  each one bends, and its drives. `guide` is SYN-BIOGENESIS's field guide
  to the colours, told for the water.

The keys are SYN-BIOGENESIS's, with this program's own where it has none:

| key or mouse | does |
|---|---|
| drag / right-drag / wheel | turn the camera / move it / zoom |
| click, right-click | select an organism (and follow it), a menu |
| `p` or space, `.` | pause, one frame while paused |
| up, down | faster, slower (5 to 960 world frames a second, or as fast as it goes) |
| `t`, `f`, del (or `k`), `g` | the selection: track (the camera follows it), feed, kill, its genes in the laboratory |
| `l` | the next look |
| ctrl+`c`, ctrl+`v` | copy the selection's code; paste it under the pointer |
| `+`, `-`, `0`, home | zoom in, out, a world unit a pixel; the whole box again |
| `m` | the minimap |
| `n`, `r` | next seed, restart |
| `s`, ctrl+shift+`s`, ctrl+`o` | save the world, save it as, open one |
| `/`, `` ` ``, `h` | the commands, the console, hide the key hints |
| F11 | fullscreen |
| esc | drop the selection, then the main menu; a screen goes back to the world |
| ctrl+`q` | quit |

**Saved worlds** (`.b3w`, in `~/.config/syn-biogenesis-3d/worlds`) hold the
parameters, the water's CO2, the dice and every organism. A world loaded goes on
exactly as the one saved would have, frame for frame. Save with ctrl+`s`
or `save [NAME]`; open with `load NAME`, or `--load NAME` on the command
line, headless too (`--headless --load FILE --save FILE`).

`--goto T` runs to time T as fast as it goes and pauses there. `--shot
FILE.ppm` draws one picture and quits (with `--seconds S`, after S
seconds). `SDL_VIDEODRIVER=offscreen` runs the window without showing it.
`--help` lists everything.

`--headless` runs the world without a window. It prints a CSV row per
report:
- the carbon (O2 and the CO2 dissolved);
- births, deaths, kills, infections and bites;
- the living segment length by colour;
- the share of flat codes and how level the bodies lie;
- with `--import` (up to four beings, each its own line), how many living
  descendants each line has (`marked`, `line2`, `line3`, `line4`).

With each row, `--census FILE` writes a row per organism (its founder's
line, where it is, how level it lies, its colours and drives), and
`--profile FILE` the water layer by layer from the surface down (its CO2,
its light and the life in it). `--set NAME VALUE` sets any parameter.

`make test`:
- checks the trigonometry and exp, and that flat codes build 0.9's bodies
  exactly;
- checks that bent bodies keep their symmetry and mirror images;
- checks the water: light by depth and in shadow, CO2 spreading without
  loss, the dead sinking to the floor, children budding in all 26
  directions alike, and sight that needs light while smell does not;
- balances the carbon and energy books on every frame of five worlds, from
  a slab a body thick to a column 900 deep;
- checks that beings keep every gene and drive through a .bgg, and that a
  world saved and loaded goes on exactly as one that never stopped;
- checks that the statistics' history and family tree leave the world as
  it would have been, and go on across a save;
- checks the telescope: genesis made a founder at a time is the same world,
  every founder of three seeds cracks back to the state that began it with
  its seed in its basin at its slot, and a mutated child finds its founder;
- checks that the optimised, unoptimised and (if installed) clang builds
  reach the same state from the same seed;
- replays twenty arena rounds in the window's way and matches the workers'
  worlds.

`tools/film.sh OUT.mp4 [options]` films a world on the CPU, without a GPU.
`make tools/view` builds the CPU window that came before the renderer, for
machines without OpenGL 3.3.

### Windows

For now the Windows build stops with errors: the arena, telescope and
winner search that came over from SYN-BIOGENESIS start their workers
through `src/syn/arena_os.c`, which so far has only Linux's half, and
`src/io/bgg.c` writes a code to memory with `open_memstream`, which MinGW
lacks. Until those are ported, what follows describes the build as it
was before they came.

The same program builds on Windows 10 and 11 with
[MSYS2](https://www.msys2.org), as SYN-BIOGENESIS does. In its UCRT64 shell:

```sh
pacman -S --needed make mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf \
    mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-SDL2_ttf mingw-w64-ucrt-x86_64-libepoxy
make
make dist
make test
```

`make` builds `syn-biogenesis-3d.exe`, linked statically: SDL and the
libraries it uses are inside it, so it needs no DLL but Windows's own and
runs from Explorer or any shell, with the `assets` folder beside it.
`make dist` puts it, without its debugging information, in
`dist/syn-biogenesis-3d/` with the assets; that folder runs on any 64-bit
Windows 10 or 11 without MSYS2.

It is a window program. Run from Command Prompt or PowerShell, `--help`
and `--headless` write to that console, which does not wait for it; piped
(`| Out-Host`, `| more`), it waits. Saved worlds, kept beings and settings
are in `%APPDATA%\syn-biogenesis-3d`, and the library reads
SYN-BIOGENESIS's beings from `%APPDATA%\syn-biogenesis`, where it keeps
them on Windows. The arena's rounds run as hidden child processes, without
a console each.

## Credits

The rules are Biogenesis's:
- **Biogenesis**, copyright (c) 2006-2010 Joan Queralt Molina:
  <https://biogenesis.sourceforge.net/>
- **Biogenesis 0.9** by MarcoDBAA (2019).

The code comes by way of [SYN-BIOGENESIS](https://github.com/syn990/SYN-BIOGENESIS),
the C rewrite of 0.9. Each source file names what it was grown from.
`src/sim/dmath.c` holds fdlibm's sine, cosine and exponential kernels, copyright (c) 1993
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
