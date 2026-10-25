# Design

SYN-BIOGENESIS-3D grows Biogenesis 0.9's life into a box of water. It is
not a port: no 3D original exists to match frame for frame, so what
SYN-BIOGENESIS proves against the Java jar, this program proves against
itself. The same seed gives the same world with every build (GCC and clang,
optimised or not), and the carbon and energy books balance on every frame.

The first version kept 0.9's rules and only gave them a third side. Life
coped, but only where the water was crowded, and nothing in it knew there
was a third dimension: bodies were bigger and spun about another axis, and
that was all. Plants owned nearly every world (93% of living length, the
median of 24). So the rules now part from 0.9's wherever depth needs them
to, in [the water](#the-water):
- light falls from the surface and dims with depth and in shade, and a leaf
  catches the light it lies across;
- CO2 is dissolved in the water, cell by cell, and spreads slowly;
- the dead sink;
- organisms see (by the light), smell the dead, and swim where their
  inherited drives pull them, when they are hungry;
- every living body has a float, and settles at the depth whose light its
  line likes;
- children bud off in every direction, and founders start in the light.

[The ecology study](ecology/README.md) measures what this changed.

## The plan

1. **The simulation, headless** (built). 0.9's rules with 3D bodies and
   rigid-body physics, the determinism and bookkeeping tests, and the
   question the whole project rests on: does 0.9's life cope with a third
   dimension? See [the sweep](sweep/README.md).
2. **The renderer, from SYN-TREK** (built). C11 with OpenGL 3.3 core
   (libepoxy), cglm and SDL2, laid out as SYN-TREK is: `src/render/` and
   `src/platform/` are the only code that calls GL or opens windows, and
   `src/sim/` never touches either.
   - **From SYN-TREK:**
     - the window and its OpenGL 3.3 check;
     - the shader loader;
     - the lighting pipeline: an HDR scene buffer, bloom, ACES filmic
       tonemapping and FXAA. It is cut down to what a world needs, without
       the warp smear or the panels.
   - **New:**
     - instanced flesh (`src/render/bodies.c`, first built as capsules):
       one mesh, shaped in the vertex shader by each segment's tissue
       (its colour) and patterned in the fragment shader, with two ends, a
       thickness at each, a colour and a glow per segment; a cell at each
       body's hub. Each body in view is drawn by how many pixels it spans:
       a fine mesh, a coarse one, or lines (strips turned to the eye);
       bodies out of view are skipped. Classic draws every body as lines,
       still. Flashes glow past white, so bites, infections and kills
       bloom.
     - the water (`src/render/water.c`), for the looks that draw it: its
       colour all round, a sediment floor with moving caustics and
       shadows, shafts of light, motes, a rippling surface. The caustics
       are drawn each frame into a small tile that repeats (256 pixels
       for 200 units), the sediment once into another, both mipmapped, so
       the floor, the bodies and the surface read them rather than work
       them out per pixel, and they soften with distance.
     - the bloom at a quarter of the picture's size; the interface's 2D
       drawing sent in one upload a frame.
     - an orbit camera. SYN-TREK's camera chases a ship, which is not what
       a world needs.
     - smooth motion: each organism is drawn between the pose it began the
       world's last frame in and the one it ended it in.
     - picking: a ray against every body's flesh.
   - **Measured on Intel UHD 630:**
     - as capsules: 100 pictures a second for the cube at seed 17 (80
       organisms, 1,315 segments); 76 a second for 1,259 organisms and
       25,607 segments, while the world runs 60 frames a second;
     - as flesh with detail by distance (1440 x 900, the world running 60
       frames a second): the default world at seed 104, time 250 (211
       organisms, 3,385 segments) 180 a second in the Biological look, 324
       in Galaxy, 334 in Digital, 395 in Classic's lines; 2,290 organisms
       and 43,314 segments 86 in Biological, 131 in Classic. The first
       flesh, every segment fine and every pixel working out its own
       caustics, made 101 and 29.
3. **The SYN-OS interface** (under way). It is drawn over the scene in the
   same GL window.
   - **From SYN-BIOGENESIS:** the SYN-OS skin and its theme reader
     (`src/ui/skin.c`, `theme.c`), whose only drawing calls (rectangles,
     triangles, text textures) now go to a small batched GL layer
     (`src/ui/gl2d.c`).
   - **The shell** (`src/ui/shell.c`) follows SYN-BIOGENESIS's: the bar,
     notifications, the labwc menu, the drop-down console, the key hints
     and the organism's card. It runs on this program's own immediate-mode
     input (`src/ui/input.c`). Whatever the interface drew last frame is
     solid: a press there never reaches the camera.
   - **Built:**
     - the bar with the world's figures;
     - the card: state, figures, life and energy bars, the kind, its body
       turning, and Follow, Kill, Keep and Release;
     - the main and right-click menus, and the console's commands;
     - theme switching, saved, with SYN-OS's own theme by default.
   - **The kinds and the field guide** are SYN-BIOGENESIS's
     (`src/guide`), with its Color Mod branches stripped and a palette of
     0.9's eight colours alone. The colours' rules are 0.9's, so the guide's
     words hold in the box too.
   - **The library screen** is SYN-BIOGENESIS's, ported (`src/syn/library.c`,
     `src/app/library.c`): this program's own library in
     `~/.config/syn-biogenesis-3d/beings`, with SYN-BIOGENESIS's folders of
     0.9 beings read beside it as they are. A kept being's .bgg is 0.9's
     format plus an optional pitch for bent genes, declared in the DTD. The
     shipped 0.9 jar's parser reads such files and ignores the bends.
   - **The telescope** is SYN-BIOGENESIS's, its engine worked out again
     (`src/syn/telescope.c`, `src/syn/crack.c`). The generator and the
     draws of a length or turn are 0.9's, so the algebra carries over; a
     founder's run of draws does not: org_new's turn, the mirroring, arms
     and genes' number, then per gene its length, turn, a draw for whether
     it bends and two more if it does, and its colour; the dispersal, six
     drives and three draws a try at a place. A bend and a drive are
     53-bit draws as a length is, so a being's later bends and drives are
     placed in its ancestor's stream too, and pin it to its one seed where
     0.9's beings, mutated only in colour or arms, could not be.
     `tests/telescope_check.c` holds genesis made a founder at a time to
     world_genesis and finds every founder of three seeds again.
   - **The parameters screen:** 0.9's tunables and the box's, grouped, with
     a number typed or + and -; those that need a new world are marked.
   - **The statistics screen** is SYN-BIOGENESIS's, ported: its history
     (`src/syn/history.c`, a sample a time unit, merged in pairs past 4,096,
     epochs, champions, events, lines), the family tree that names species
     (`src/syn/family.c`), its five tabs (`src/app/stats.c`,
     `src/app/family_view.c`), and the file beside a saved world that keeps
     them (`src/syn/sidecar.c`). 0.9's WorldStatistics (`src/sim/stats.c`)
     are kept by the world, saved in its file (version 3), and change
     nothing in it; `tests/syn_check` checks that a world recorded is the
     world left alone, and that a history saved and opened again goes on.
   - **The arena** is SYN-BIOGENESIS's, ported (`src/syn/arena.c`, its
     record `src/syn/record.c`, its rounds `src/syn/round.c`, the family
     tree that names species `src/syn/family.c`; the screen
     `src/app/arena.c`). Workers are this program again
     (`--arena-worker`), fed rounds through a pipe; the window and
     `--arena` share the ladder.
     - The maps are 3D's: the default box, four and nine times its surface
       at its depth (founders scaled to the surface, which they start
       under), a pond and a cube.
     - The record keeps each round's parameters by name, so a round can be
       made again in the window (WATCH); `tests/arena.sh` checks that the
       rounds made again from the record are the workers' worlds, hash for
       hash.

Since built: the arena, the being library and saved worlds (`.b3w`,
`src/io/worldfile.c`).
- A saved world holds the parameters, the dice's state and every organism
  between two frames.
- It holds every genetic code once. Organisms that share a code (an
  import's copies, a virus's victims) share it again after loading, because
  the rules compare codes by identity.
- A loaded world goes on bit for bit as the saved one would have;
  `tests/check.c` checks this in a slab, the default world and a cube.

4. **The water** (built): the rules in [the water](#the-water), the
   window's light by depth, the organism's card with its depth, its light
   and its drives, and [the ecology study](ecology/README.md).

Later: the telescope (the dice are the same java.util.Random, so every gene
is still a place on its seed's stream).

## The water

The world is a box of water, width x height x `world_depth`, with the
surface on top (z = depth) and the floor at z = 0.

**Light falls from the surface.** The water dims it by e for every
`light_depth` (default 250) it goes down. Every body casts a shadow: each
segment's capsule, seen from above, darkens the water's cell it is in by
its area over the cell's floor (times `shade`), and the light that reaches
a cell is what the cells above it let through (`src/sim/world.c`,
`build_light`). The cells are at most 32 units a side.

The light reaches about as deep as the default world: a 300-deep floor has
30% of the surface's. At first it fell by e every 100, and with every body
able to choose its depth (below) all chose the top: in 48 worlds the
living keep between 2 and 77 deep (the 10th and 90th centiles over times
300 to 400; median 12), so the lower three quarters of the water hold about a
tenth of them. With 250 they live from 7 to 236 deep (median 82). The
winners are as mixed either way (the plants hold a median 12% of the
living length with 100, 22% with 250; hunting lasts in 45 and 46), so 250
is chosen for the depth it gives life.

**Days, if wanted.** With `day_length` above 0 the surface's light follows
a day of that many time units: half at sunrise (a world begins at dawn),
full at noon, none at midnight. Every body that likes some light then
follows it, down in the morning and up at dusk, as plankton do. It is off
by default, for what it did to the worlds: a day halves the mean light,
and with days of 8, 10 of 48 worlds died out, hunting lasted in 24, the
plants' lines won 28, and half the living stayed within 25 of the
surface. An earlier build also gave every body a short sense that works
in the dark; with days it made things worse (hunting in 21 of 48, against
31 on that build without it), so it was taken out.

**A leaf catches the light it lies across.** A green segment makes energy
from its length as seen from above (a level leaf all of it, an upright one
none) times the light at its middle, where 0.9 used its plain length. The
CO2 around it then limits that as 0.9's CO2 did (below).
- **Leaves turn to the light.** Each living leaf twists its body toward
  lying level, by its length (`leaf_turn`). A flat body can lie wholly
  level; a bent one cannot. So plants now have a reason to stay flat, and
  swimmers (below) a reason to bend.

**CO2 is dissolved in the water.** 0.9's CO2 was one number for the world.
Here every cell of the water holds its own, and:
- a leaf takes from the cell its organism's centre is in, at 0.9's rate
  scaled by the concentration there over `co2_saturation`;
- breathing, a bite's tenth, overflowing energy and a corpse's decay give
  CO2 back to the cell where they happen;
- every frame each cell trades with its six neighbours by the difference
  between them (`co2_diffusion`, square units a frame). What one gives the
  other takes, so the total only changes by rounding.

At genesis every cell holds `co2_density` per million cubic units: 0.9's
5000 in its 1000 x 1000 floor, 32 deep. So a bigger world holds more
carbon, and more life. In 0.9 the CO2 a world held was fixed whatever its
size, and the sweep found that a deeper world was the same few organisms
spread through more water.

Slow spreading is what makes depth matter for carbon. A dense bed of
plants empties the water around it and grows only as fast as CO2 comes
back, from the cells around and from whatever breathes and decays beside
it, while the dead carry carbon down to water that only slowly gives it
back. Plants that spread out do better than plants that crowd, and the
richest water lies below the brightest. The default, 5, was chosen over
20 and 3, 48 worlds each, before the float and the deeper light (below and
above): at 5 the worlds were won most evenly by plants, mixed lines and
hunters (19, 16 and 12 of 48), every world but one lived, and plants lived
deepest. At 20 the results were close (19, 20 and 8; one died out); at 3
the water starved (3 died out, hunters won 3).

**The dead sink.** A corpse gains `sink` (0.01) of downward speed every
frame, against the water's drag (0.9's rubbing), so it falls about half a
unit a frame. On the floor (or under the surface) it stops instead of
bouncing. The dead carry their carbon down and decay where they come to
rest.

**Organisms sense.** Every 8 frames (each organism on its own beat) an
organism looks around. Its genetic code carries six drives, each -1 (away)
to 1 (toward):
- `light`: toward the light it likes, (1 + drive) / 2 of noon's at the
  surface, as strongly as the drive is far from 0. 1 seeks the surface and
  -1 the dark; between, an organism settles at the depth whose light it
  likes;
- `carbon`: toward water with more dissolved CO2;
- `plants`, `hunters`: toward living bodies by how much of them is green,
  and how much red, gray and white;
- `dead`: toward corpses;
- `crowd`: toward any living body.

Each body within `sight` (120) pulls by the drives that care about it, the
more the nearer. Its parent and its children are left out, as 0.9 spared
them harm. The pulls add up to the organism's aim.
- **Sight needs light.** A living body is seen only as far as the light on
  it allows, by its square root (as the noise in few photons would have
  it): from 120 away at the surface, 73 a light depth down, 27 three light
  depths down. So shade and depth hide prey, and hunting is easiest near
  the surface.
- **The dead are smelled,** light or not, from the whole of `sight`, so the
  floor can be scavenged.
- **Hunger draws.** A pull toward something is scaled by how hungry the
  organism is: 1 with no energy, 0 with the energy for a child. A pull away
  is not: what an organism shuns, it shuns full or not.

Hunger came last, and for a reason. Before it, a hunter that could see and
reach its prey chased it however full it was. In the first 8 worlds of a
sweep without it, 3 died out; one of them had grown to 82 organisms and
fell to 4 within 50 time units, through 528 infections, and two others ran
at 20,000 bites per 50 units. A fed hunter now drifts, and its prey
recovers.

A new random code draws each drive anywhere in -1 to 1, and a mutation (the
usual 5% per trait) draws it anew, as 0.9 draws a mutated gene's length or
colour anew. Drives that a mutation only nudged drifted for hundreds of
generations before selection could find anything, and hunters stayed at
the surface while their prey had gone deep.

**Every living body has a float.** It rises or sinks toward its aim's up
or down (`rise` 0.004 of speed a frame, against the drag: at most about 50
units a time unit), whatever its shape, cilia or none. Without it, a body
without cilia never changed its depth, and life lived in the sheet its
founders were born in: watched, it still looked 2D. Measured over 2 time
units in one world, bodies without cilia moved 0 units up or down and
swimmers 10, against 221 to the side.

**Cilia swim along the aim, and turn the body to it.** A cyan segment's
push is 0.9's (12 x the segment over the mass, 8 frames in 100), along the
segment, whichever way along it is nearer the aim. Its spin, 0.9's size,
turns that way toward the aim, the more the further off it points, so a
swimmer comes round to face where it is going, up and down included.
Without an aim both are 0.9's: the push from the segment's start to its
end, the spin of random sense. Leaves that want to lie level turn a mixed
body back, so the more green a swimmer carries, the harder it is for it to
dive or climb.

**0.9's beings arrive with instincts.** A .bgg from 0.9 has no drives,
since 0.9 had no senses. Rather than arrive blind, it is given drives read
from its body (`gc_instincts`): leaves seek the light (by their share of
the length) and keep away from hunters; red, gray and white seek plants
(twice their share, at most 1); red seeks the dead too. Its descendants'
drives then evolve like any others. A being saved from this program keeps
its own.

**Children bud off in every direction.** 0.9 placed a child in one of 8
directions around its parent on the plane, and an earlier version of this
program tried those 8 first and only then the 18 above and below. Every
line grew into a sheet. Now all 26 directions are tried, from a random one
(so each is as likely to come first) and on by a random stride that shares
no factor with 26 (so which comes after a full one is the dice's choice,
not a list's neighbour). The child lands as far from its parent's centre
as both bodies reach, and a unit more, in every direction alike (further,
if their boxes would still touch). 0.9's rule, a unit apart box to box,
put a flat parent's children about 40 units to its side and 3 above it.

**Founders start in the light.** A random founder (or an imported being)
is placed at random in the top two light depths of the water, or anywhere
if that is too thin for it. Placed anywhere, most founders of a deep world
began in the dark and died there before the world began: in a column 900
deep, with the light falling by e every 100, 3 of 12 worlds died out, and
with founders in the light, none did.

### Heights, methane and body plans

Measured on the rules before these (12 worlds, seeds 100 to 111, time
200): 46% of the living were in the top three tenths of the water, 29% in
the middle four, and a cluster of 11% sat on the floor. Almost all of
them carried leaves (1,574 of 1,600). The floor's lot were plants whose
light drive asked for darker water than the box has, and nothing lived
on the dead lying there (43 of the 107 corpses). How high a body kept was
a side effect of the light it liked, and low was simply worse: a leaf at
the floor makes about half what one near the surface does (light 0.29
against 0.92 of the surface's, CO2 125 against 72 per Mu^3, saturating
at 156).

So:
- **Every line keeps to a height of its own**, a gene (`height`, 0 the
  floor to 1 the surface) drawn for founders, inherited and drawn anew by
  a mutation like the drives. Every living body is pulled toward it, all
  the way from a fifth of the water off: cilia swim it there, a float
  takes a body without them. Founders begin at their height. The light
  drive no longer moves a body up or down; it steers across, out of the
  shade or into it. A code saved before the height gene keeps to the
  height whose light its light drive liked.
- **The dead rot into methane** where they lie (Color Mod's
  decomposition), and since they sink, it pools low; it spreads as CO2
  does and turns back into CO2 over `ch4_oxidation` frames. O2 + CO2 +
  CH4 is the world's carbon, checked every frame.
- **Purple**, Color Mod's methane eater, is a colour here: by its whole
  length, whatever the light, it makes energy from the methane around it,
  as a leaf does from CO2 (`methane_divisor`, `ch4_saturation`).
- **Body plans**: a code's arms stand as 0.9's disk, as a cone (each arm
  tilted out of the disk by the code's cone angle) or as an orb (spread
  over a sphere: two up and down, four a tetrahedron, six an octahedron,
  eight a cube). A 0.9 code is a disk, its body exactly 0.9's.
- **Cilia swim steadily**: each frame a stroke worth what 0.9's random
  kicks gave on average, along the segment as far as it points where the
  body is going, turning it to face there. With nowhere to go they kick
  at random as 0.9's did.

The same 12 worlds afterwards:

| | time 200, before | time 200 | time 500 |
|---|---|---|---|
| living | 1,600 | 2,092 | 2,048 |
| in the top three tenths | 46% | 47% | 48% |
| in the middle four | 29% | 40% | 37% |
| in the bottom three | 24% | 13% | 15% |
| on the floor (the lowest tenth) | 175 | 43 | 57 |

Each tenth's living keep to their own height (the mean height gene by
tenth runs from 0.89 at the top to 0.17 at the floor at time 200, 0.13 at
time 500). The hunters are spread much as before: 58% of the living with
red parts are in the top three tenths at time 500, against 65% before,
and every tenth has some. Methane pools low (6.7 per Mu^3 at the top, 16
at the floor at time 500). Purple spreads at every height, but almost
always beside leaves: a body living on methane alone is rare (15 of 2,048
at time 500), since methane comes only from the dead and the floor's
flux feeds few. The floor is still the thinnest tenth: low is still
poorer, now for a lineage that has chosen it rather than one stuck
there. Orbs grow rarer with time (from 25% of founders to 9% of the
living at time 500); cones hold on (30%).

## Kept from 0.9, unchanged

- **Every energy rule but light's:**
  - red's bite (half its length, a tenth breathed out);
  - white's infection, gray's kill, blue's shield, yellow's extra children;
  - parents and children never harming each other;
  - upkeep of mass / 5000 a frame;
  - ageing (31 units of 256 frames);
  - reproduction at 40 + 3 x genes x symmetry, at most every 20 frames;
  - the overflow above twice that, breathed away;
  - corpses decaying at 0.1 a frame.
- **The genetics:** 5% mutation per trait, 1 to 100 genes, symmetry 1 to 8,
  mirroring, dispersal, the colour odds, the 16 growth stages. The drives
  are new, and mutate at the same rate.
- **The frame:**
  - organisms move in birth order;
  - a child born during a frame neither moves nor can be touched until the
    next frame;
  - a move that ends in a touch, or outside the world, is undone.
- **The dice:** one java.util.Random stream for every decision, as in 0.9
  and SYN-BIOGENESIS.
- **0.9's parameters** and their defaults, except the world's size (now
  600 x 600 x 300), its founders (30, for a world of three times the old
  one's water) and its CO2 (a density instead of a total).

## Changed, and why

**Genes get a pitch.** A gene is a length, a turn (theta) and a colour, plus
a new `pitch`: how far the segment bends up or down out of the plane the
turn happens in. Pitch 0 is a 0.9 gene, so every 0.9 genetic code is a 3D
code whose body is flat, and imports need no conversion.
- A new random gene bends with probability `pitch_prob` percent (default
  50), by an angle anywhere from straight down to straight up.
- In mutation, 0.9 drew a fourth chance per gene ("back") and never used
  it. That draw is now the pitch's mutation, so a copied gene takes the
  same four draws it took in 0.9.

**The body is drawn by a turtle that knows which way is up.** 0.9 drew each
arm as a turtle on the plane. Here the turtle also carries an up vector:
theta turns the heading about up, and pitch bends the heading toward up.
- Symmetric arms are copies turned about the body's axis.
- A mirrored arm is its neighbour's reflection in a vertical plane: turns
  reversed, bends kept.
- With every pitch 0, up never moves and the body is exactly 0.9's, in the
  plane z = 0. `tests/check.c` checks this against 0.9's own drawing for
  400 random codes, and checks symmetry and mirroring for bent ones.

**Segments are capsules.** Two lines in space practically never cross, so a
segment is a line with thickness `segment_radius` (default 1). Two segments
touch when their axes come closer than two radii. As in 0.9, segments
shorter than 1 (a newborn's) don't touch.

**The body is a rigid body.** 0.9 kept a position, an angle and the scalars
dx, dy, dtheta and inertia. Here an organism has a position, an orientation
(a quaternion), a velocity, a spin vector and an inertia tensor. The tensor
is that of rods of density 1, plus each capsule's thickness as a solid
cylinder's. Since there is no original to match, the physics uses the
textbook formulas:
- It turns about its centre of mass. 0.9 turned it about the middle of its
  bounding box.
- 0.9's inertia added each segment's cy^2 without its mass, and its impulse
  had a typo (`rbpy * ny`). SYN-BIOGENESIS keeps both for fidelity; neither
  is carried here.
- A free body keeps its angular momentum as it turns, so a tumbling body
  precesses.
- The collision impulse is the standard rigid-body impulse with 0.9's
  elasticity, applied only while the bodies are closing. 0.9 applied it to
  separating bodies too, which pulled them together.
- The contact normal runs from the mover's segment to the other's as they
  were before the move. After a 5-unit move a thin pair may already have
  passed through each other, and their closest points would point the
  wrong way.
- Speed and spin are clamped by length, everywhere they change. 0.9 clamped
  dx and dy separately, so diagonal motion could reach 5 x sqrt(2).
- Growing or shrinking keeps kinetic energy, as in 0.9: speeds scale with
  the change of mass, within the limits. A growth step that is undone (the
  bigger body didn't fit) scales them back. 0.9 kept the boost, so a
  starving body stuck against a neighbour sped up every frame.
- A cilium's push is 0.9's: 12 x the segment over the mass. Its spin keeps
  0.9's size and random sense; its axis is the one a push along the segment
  would turn the body about. For a flat body lying level that is 0.9's axis,
  straight up.

**Which way is up is inherited.** Founders lie level: their up axis is the
world's, turned by the random angle 0.9 drew as theta. A child buds off its
parent: the parent's orientation, turned about the parent's up axis by that
same random angle. Tilt comes from collisions and cilia, and leaves turn
their bodies back toward level (see [the water](#the-water)).

**The books always balance.** In 0.9, a founder that found no room took its
energy out of the CO2 anyway, and that energy vanished. Here a founder takes
its energy only once it has a place. So these always hold, up to rounding,
and are checked on every frame:
- energy in all bodies, plus the crumbs (< 10^-7 each) that left with
  removed organisms, equals O2;
- O2 + CO2 never changes;
- the water's cells hold all the CO2.

**The world is a box.** A living body crossing a side bounces off it, and
its spin stops, as at 0.9's edges; a corpse stops at the floor and the
surface. 0.9's quirk of killing a body of one pixel or less sitting exactly
at (0, 0) is dropped.

**Neighbours come from a grid.** 0.9 used JTS's two-dimensional STRtree.
Here the frame-start centres are sorted into 32-unit cells, and a body asks
the cells within reach of its box. "Reach" is the biggest body's size plus
one frame's travel, so nothing that can touch is missed.

**The trigonometry is the program's own.** The C library's sin, cos and exp
differ in their last bits between glibc, the Windows CRT and macOS, so the
simulation uses fdlibm's kernels and range reduction written out in IEEE
arithmetic (`src/sim/dmath.c`). It is compiled with floating-point
contraction off, so no FMA changes a result. The program refuses to build
where doubles keep wider intermediates (x87 arithmetic, `FLT_EVAL_METHOD`
2); every x86-64 and ARM64 target rounds as it goes.

## New parameters

| parameter | default | what it is |
|---|---|---|
| `world_depth` | 300 | the third side of the world; the surface is on top |
| `segment_radius` | 1 | how thick a segment is; contact is closer than twice this |
| `pitch_prob` | 50 | percent of new random genes that bend out of their plane; 0 keeps life flat |
| `co2_density` | 156.25 | CO2 dissolved per million cubic units at genesis (0.9's 5000 in 1000 x 1000 x 32); replaces 0.9's `initial_co2` |
| `co2_saturation` | 156.25 | the concentration at which leaves work at full rate; replaces 0.9's `drain_subs_divisor` |
| `co2_diffusion` | 5 | how fast dissolved CO2 spreads between cells, square units a frame |
| `light_depth` | 250 | the depth over which light falls to 1/e; 0, no dimming |
| `day_length` | 0 | time units from one dawn to the next; 0, always noon |
| `rise` | 0.004 | the speed a living body's float gains a frame toward its aim's up or down |
| `shade` | 1 | how much of the light a body's shadow stops |
| `leaf_turn` | 0.1 | how hard leaves turn their body to lie level |
| `sink` | 0.01 | the speed the dead gain downward every frame |
| `sight` | 120 | how far an organism sees in full light, and smells the dead |
| `cone_prob`, `orb_prob` | 25, 25 | percent of new random codes whose arms stand as a cone, an orb (the rest, 0.9's disk) |
| `purple_prob` | 10 | the weight of purple among new genes' colours, beside 0.9's seven |
| `purple_energy_consumption` | 0 | what a purple segment's use costs |
| `ch4_saturation` | 20 | the methane per million cubic units at which purple works at full rate |
| `methane_divisor` | 500 | purple's yield divisor: its length over this, a frame, at saturation |
| `ch4_oxidation` | 2000 | frames over which dissolved methane turns back into CO2 (the time constant) |

Any of them can be set from the command line (`--set NAME VALUE`) or the
console (`set NAME VALUE`).
