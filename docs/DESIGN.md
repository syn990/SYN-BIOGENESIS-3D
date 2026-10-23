# Design

SYN-BIOGENESIS-3D grows Biogenesis 0.9's rules into a box. It is not a
port: no 3D original exists to match frame for frame, so what SYN-BIOGENESIS
proves against the Java jar, this program proves against itself. The same
seed gives the same world with every build (GCC and clang, optimised or
not), and the carbon and energy books balance on every frame.

## The plan

1. **The simulation, headless** (this step). 0.9's rules with 3D bodies and
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
     - instanced capsules: one `glDrawElementsInstanced` for every segment
       of every organism, with two ends, a radius, a colour and a glow each.
       Flashes glow past white, so bites, infections and kills bloom.
     - an orbit camera. SYN-TREK's camera chases a ship, which is not what
       a world needs.
     - smooth motion: each organism is drawn between the pose it began the
       world's last frame in and the one it ended it in.
     - picking: a ray against every capsule.
   - **Measured on Intel UHD 630:**
     - 100 pictures a second for the cube at seed 17 (80 organisms, 1,315
       segments);
     - 76 a second for 1,259 organisms and 25,607 segments, while the world
       runs 60 frames a second.
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
   - **Built:** the bar with the world's figures; the card (state, figures,
     life and energy bars, a kind and a sentence true of the organism, its
     body turning, Follow, Kill and Release); the main and right-click
     menus; the console's commands; theme switching, saved, and SYN-OS's
     own theme by default.
   - **To come:** the parameters screen, the being library and SYN-BIOGENESIS's
     field guide and species kinds, in place of the card's simple kinds.

Later: the arena, the telescope (the dice are the same java.util.Random,
so every gene is still a place on its seed's stream), the being library and
saved worlds.

## Kept from 0.9, unchanged

- **Every energy rule:**
  - photosynthesis by green length and the CO2 left;
  - red's bite (half its length, a tenth breathed out);
  - white's infection, gray's kill, blue's shield, yellow's extra children;
  - parents and children never harming each other;
  - upkeep of mass / 5000 a frame;
  - ageing (31 units of 256 frames);
  - reproduction at 40 + 3 x genes x symmetry, at most every 20 frames;
  - the overflow above twice that, breathed away;
  - corpses decaying at 0.1 a frame.
- **The genetics:** 5% mutation per trait, 1 to 100 genes, symmetry 1 to 8,
  mirroring, dispersal, the colour odds, the 16 growth stages.
- **The frame:**
  - organisms move in birth order;
  - a child born during a frame neither moves nor can be touched until the
    next frame;
  - a move that ends in a touch, or outside the world, is undone.
- **The dice:** one java.util.Random stream for every decision, as in 0.9
  and SYN-BIOGENESIS.
- **The parameters** and their defaults.

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
same random angle. Tilt therefore comes only from physics (collisions and
cilia), and a thin world keeps its bodies level.

**Births look in 26 directions.** First the 8 directions around the
parent on the plane, in 0.9's order, from a random one, exactly as 0.9 did.
If none has room, the 18 above and below, from a random one, ordered so that
each direction above is followed by the one below it. A parent under the
ceiling (or on the floor) then sends its children to every direction below
(or above) equally often.

**The books always balance.** In 0.9, a founder that found no room took its
energy out of the CO2 anyway, and that energy vanished. Here a founder takes
its energy only once it has a place. So these always hold, up to rounding,
and are checked on every frame:
- energy in all bodies, plus the crumbs (< 10^-7 each) that left with
  removed organisms, equals O2;
- O2 + CO2 never changes.

**The world is a box** of width x height x `world_depth` (default 32). A body
crossing a side bounces off it, and its spin stops, as at 0.9's edges. 0.9's
quirk of killing a body of one pixel or less sitting exactly at (0, 0) is
dropped.

**Neighbours come from a grid.** 0.9 used JTS's two-dimensional STRtree.
Here the frame-start centres are sorted into 32-unit cells, and a body asks
the cells within reach of its box. "Reach" is the biggest body's size plus
one frame's travel, so nothing that can touch is missed.

**The trigonometry is the program's own.** The C library's sin and cos
differ in their last bits between glibc, the Windows CRT and macOS, so the
simulation uses fdlibm's kernels and range reduction written out in IEEE
arithmetic (`src/sim/dmath.c`). It is compiled with floating-point
contraction off, so no FMA changes a result. The program refuses to build
where doubles keep wider intermediates (x87 arithmetic, `FLT_EVAL_METHOD`
2); every x86-64 and ARM64 target rounds as it goes.

## New parameters

| parameter | default | what it is |
|---|---|---|
| `world_depth` | 32 | the third side of the world |
| `segment_radius` | 1 | how thick a segment is; contact is closer than twice this |
| `pitch_prob` | 50 | percent of new random genes that bend out of their plane; 0 keeps life flat |
