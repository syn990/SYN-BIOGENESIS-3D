# The dice telescope in 3D

SYN-BIOGENESIS reads Biogenesis 0.9's random generator as algebra: the
telescope (`src/syn/telescope.c`) and its cracking engine
(`src/syn/crack.c`). This program uses the same generator
(java.util.Random, 48 bits) and draws a gene's length and turn exactly as
0.9 did, so the algebra carries over unchanged: a state n draws away is
one affine map, a seed is its 48 low bits, and a length read exactly
leaves almost always one state that could have drawn it. What does not
carry over is everything that depends on the order of the draws. That is
worked out again here.

## A founder's run of draws

In 0.9 a founder took org_new's angle (2 draws), the mirroring, arms and
gene count (1 each), then 5 draws per gene. Here:

| draws | what |
|---|---|
| 2 | org_new's turn about the body's own axis |
| 1 + 1 + 1 | mirroring, arms, gene count |
| 2 + 2 + 1 (+2) + 1 per gene | length, turn, whether it bends (and the bend if it does), colour |
| 1 | dispersal |
| 12 | six drives, 2 each |
| 2 + 1 + 2 | the height its line keeps to, its body plan, its cone's tilt |
| 3 per try | a place in the water, up to twelve tries |

So gene j's length is drawn `5 + 6j + 2m` draws after the founder was
begun, m the genes before it that bend. A length cracked to its state steps
back to the founder's start in at most 36 ways (j < 8, m <= j); drawing a
founder again from each, the one whose gene j is that length is it.

A bend (`(u - 0.5) pi`), a drive (`2u - 1`), a height (`u`) and a cone's
tilt (`(u - 0.5) pi`) are 53-bit draws as a length is, so they crack too. A being's bends, drives, height and cone drawn
since its ancestor are placed in the ancestor's stream along with its
lengths and turns, and they pin it to its one seed: 0.9's beings, mutated only in a
colour or the arms, could not be.

## Checks

`tests/telescope_check.c` (in `make test`):
- genesis made a founder at a time (crack_genesis_*) is world_genesis, to
  the hash;
- every founder of seeds 7, 2026 and 99991 (12 founders each) cracks back
  to the state that began it, and its seed is in that state's basin with
  the founder's slot and organism: 36 of 36;
- a child several mutations on still finds its founder.

By hand, on worlds whose seeds were known:
- `--telescope world` on seed 424242 saved at frame 7,680: found from the
  first gene length tried, the tenth seed replayed, identical to the save
  at frame 7,680, in 4.8 s; from the window on seed 7 at time 20, the first
  seed replayed;
- `--telescope find` on a generation-7 descendant from that world: its
  ancestor (founder 10, 3 mutations), 383 seeds whose genesis makes it,
  two lengths drawn since, and seed 424242 picked out by them;
- `--telescope find` on a being from seed 7 (LAEXERA sp., 5 mutations, no
  length or turn drawn since): its light and hunters drives and two bends
  drawn since, placed in the stream, and seed 7 picked out of 125;
- the scene made for its "alive" frame (6,656) holds it.

## The fitness: which founder's bloodline leads

FORGE A WINNER weighs a seed by its founders without running it, and
Create a winner ranks random candidates the same way. SYN-BIOGENESIS's
weighing (a plant's growth rate from its code, fitted to 0.9 worlds) does
not hold in 3D: light falls with depth, cilia steer, hunting lasts. It was
fitted again here.

Data: `tools/founders 1000 1300 300` (six runs of 50 seeds at once), the
default world (600 x 600 x 300, 30 founders), each run to time 300: every
founder's code and where it began, and which founder's bloodline had the
most living at the end. A bloodline was alone by then in 33 of the 300
worlds; in the rest several were still alive.

Model (`tools/crack-fit.py`): a conditional logit, one choice per world
among its founders, on the founder's code: the log of each colour's
length in all arms, the log of its reproduction energy, its arms, the
share of its genes that bend, its six drives, the light drive times the
log of its green, the height its line keeps to, and whether it is a cone
or an orb. Five folds by world:

| | per world, held out |
|---|---|
| log-likelihood | -2.14 (uniform: -3.40) |
| the fittest founder leads | 33.0% (uniform: 3.3%) |

Where a founder began (the light there) added nothing (33.0%, -2.13):
founders begin at their height, so the code says it already. The fitness
is the code's alone. Fitted on all 300:

| feature | coefficient |
|---|---|
| log(1 + green) | +1.03 |
| log(1 + red) | -0.16 |
| log(1 + cyan) | -0.08 |
| log(1 + blue) | -0.35 |
| log(1 + white) | -0.13 |
| log(1 + gray) | -0.30 |
| log(1 + yellow) | +0.06 |
| log(1 + purple) | -0.09 |
| log(reproduction energy) | -1.10 |
| arms / 8 | -1.29 |
| share of genes bent | -0.14 |
| drives: light, carbon, plants, hunters, dead, crowd | +0.19, +0.15, +0.03, -0.08, -0.04, -0.26 |
| light drive x log(1 + green) / 5 | +0.25 |
| the height its line keeps to (0 the floor, 1 the surface) | +1.44 |
| a cone, an orb (against a disk) | -0.03, -0.10 |

Green, a line that keeps high, few arms and cheap reproduction win; a
founder that seeks company does worse, and the body plan matters little.
The fitness fitted before the height gene, methane and body plans (green,
light-seeking, few arms) still picks the leader in 22.0% of these worlds.

The forge's own picks: of 20,000 seeds weighed (4.9 s), the 12 likeliest,
each given 65% to 75% (about 8.2 expected), run to time 300: the expected
founder's bloodline led in 9. None of the twelve is among the fit's own
worlds.

## Create a winner

`src/syn/winner.c`, SYN-BIOGENESIS's, with places in the water and the
save carrying the parameters and the generator. On seed 424242's world at
time 30, a 60-second search predicted its winner's bloodline as 8 of the
102 living at time 46, with the world's hash then; the winner pasted into
the saved world independently and run to time 46 gave that hash. In the
window, a winner placed the same way came out "as calculated" when time
46 came.
