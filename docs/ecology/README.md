# Who wins in the water?

> These runs were made on the rules before the height gene, methane,
> purple, body plans and steady swimming (docs/DESIGN.md, "Heights,
> methane and body plans"); they describe those rules.

Under the first rules (0.9's, in a box) the plants won: in the default
world they held a median 94% of the living length, and hunting lasted to
the end in 21 of 48 worlds. Under the water's rules they hold 22%, hunting
lasts in 46 of 48, and the founders whose lines win are 18 plants, 16 mixed
and 14 hunters (29, 17 and 2 before). Life also uses the depth: the middle
80% of the living lie between 7 and 236 units down a 300-deep world, the
plants deeper than the rest.

What is not solved: in the deep, dark shapes (the cube and the column) the
plants still hold most of the living, and hunting fades in 11 of 24
columns; lines that hunters found usually turn green as they go, so at the
end hunters are what most of the living are in only 3 of 48 worlds; and
no hunter among 0.9's beings reaches the arena's top six.

## How it was measured

`tools/ecology.py run` runs the headless program, and `tools/ecology.py
report` writes [tables.md](tables.md). With 6 cores the new rules' runs
take about 85 minutes.

- **Worlds:** 30 random founders (0.9's 15 in the first rules' world), 400
  time units (102,400 frames), the census taken every 50. Four shapes of
  water:
  - **default:** 600 x 600 x 300, 48 seeds;
  - **pond:** 1,000 x 1,000 x 64, wide and shallow, 24 seeds;
  - **cube:** 400 x 400 x 400, mostly dark, 24 seeds;
  - **column:** 300 x 300 x 900, a little surface over a lot of dark, 24
    seeds.
- **Beings:** the 18 0.9 beings of the first study (your library and the
  original website's gallery), each 10 copies, alone and among 30 random
  founders, 8 seeds, 200 time units, in the default world. They arrive with
  the drives their bodies ask for (see [the water](../DESIGN.md#the-water)).
- **The arena:** every pair of those beings, 4 rounds each (612 rounds),
  10 copies of each and nothing else, 200 time units, in the default world.
  A round goes to the line with more living at its end.

Each world is classed at its end:
- **hunting lasts** if there is at least one bite, kill or infection a time
  unit over the last quarter;
- every living organism is a **plant** (under a tenth of its length red,
  gray or white), **mixed** (that, and at least 40% green) or a **hunter**
  (less green);
- **the winner** is the founder whose line holds the most living length;
  the table gives the kind that founder was at genesis, and, separately,
  the kind most of the living are at the end.

With 48 worlds a share has a standard error of about 7 points, and with 24
of about 10; read smaller differences as noise.

**The first rules**, for comparison, are the build of commit 93fd463 with
[first-rules-census.patch](first-rules-census.patch) applied: a census and
the line each organism descends from, written out and nothing else. Every
world it makes has the same fingerprint as that commit's (checked on two
seeds and shapes). They run in their own default world, 1,000 x 1,000 x 32
with 15 founders, and in a cube of its volume, 318 x 318 x 317:

```sh
git worktree add ../first 93fd463 && cd ../first
patch -p1 < ../SYN-BIOGENESIS-3D/docs/ecology/first-rules-census.patch
make NO_WINDOW=1 && cd ../SYN-BIOGENESIS-3D
RULES=first BIN=../first/syn-biogenesis-3d tools/ecology.py run
RULES=first tools/ecology.py report      # docs/ecology/first-rules.md
```

## The default worlds

Each rule set in its own default world, 48 seeds. Shares are medians over
the living worlds; depths are medians over the worlds of the census at
times 300, 350 and 400.

| | first rules, 1,000 x 1,000 x 32 | water, 600 x 600 x 300 |
|---|---|---|
| worlds alive at the end | 48 | 48 |
| hunting lasts | 21 | 46 |
| organisms | 77 | 172 |
| living length: plant / mixed / hunter | 94% / 5% / 0% | 22% / 55% / 16% |
| worlds where plants hold 90% of it | 28 | 2 |
| worlds where hunters hold a quarter of it | 4 | 18 |
| founders who won: plant / mixed / hunter | 29 / 17 / 2 | 18 / 16 / 14 |
| most living now: plant / mixed / hunter | 41 / 3 / 4 | 18 / 27 / 3 |
| depth of the living: 10th / 50th / 90th centile | 11 / 16 / 21 | 7 / 82 / 236 |
| median depth: plant / mixed / hunter | 16 / 16 / 16 | 115 / 59 / 51 |
| how level bodies lie: plant / mixed / hunter | 0.51 / 0.54 / 0.52 | 0.67 / 0.67 / 0.59 |

A hunter founder's line wins 14 worlds, but at the end 11 of those lines
are mostly mixed or plants (3 are still mostly hunters): the lines that
win by hunting take up leaves as they go. Under the first rules 41 of 48
worlds ended mostly plants, whoever founded the winning line.

The kinds part by depth. Half of the hunters and half of the mixed live in
the top fifth (60 units); the plants settle wherever their line's liked
light is, a third of them in the top fifth and nearly a quarter deeper
than 200. Under the first rules everything shared one 32-deep slab.

"Level" is how near the body's up axis is to the world's (1 level, 0 on
its edge, 0.5 for a body turned at random). The first rules' bodies lie at
random; the water's turn their leaves to the light.

## The other shapes

| | worlds | alive | hunting lasts | living length: plant / mixed / hunter | plants hold 90% | depth: 10th / 50th / 90th |
|---|---|---|---|---|---|---|
| pond, 1,000 x 1,000 x 64 | 24 | 24 | 23 | 36% / 43% / 11% | 1 | 5 / 33 / 60 |
| cube, 400 x 400 x 400 | 24 | 23 | 18 | 61% / 31% / 9% | 6 | 6 / 106 / 260 |
| column, 300 x 300 x 900 | 24 | 22 | 13 | 70% / 24% / 1% | 7 | 3 / 119 / 266 |
| *first rules' cube, 318 x 318 x 317* | 24 | 23 | 6 | 95% / 5% / 0% | 16 | 40 / 148 / 262 |

The pond, shallow and lit to the floor, comes out near the default
world. The cube and the column, whose water is mostly dark (the light
falls to a third by 275 down), go more to the plants, and hunting fades in
more of them; these runs do not show why. Against the first rules' cube,
where the light did not dim and bodies spread through all of it, the
water's still does better: hunting lasts in 18 of 24 against 6, and the
plants hold 61% against 95%.

## 0.9's beings

The full tables are in [tables.md](tables.md) and, for the first rules,
[first-rules.md](first-rules.md).

**Alone,** most hunters now die out: #305, gallery_1 and gallery_6 live in
none of the 8 worlds, gallery_3 in 1 and gallery_2 in 4 (under the first
rules #305, gallery_2 and gallery_3 lived in all 8). CHAMPION, 30% green,
still lives in all 8. The plants and the mixed all live alone in all 8,
and number 120 to 164 after 200 units; under the first rules CHAMPION (2)
and ultimate-syn1 died out alone in most worlds, and the rest numbered 28
to 82.

**Among 30 random founders,** the pure plants lost their hold: ultimate
(all green) held all the living in its median world and now holds 13%;
being-496 100% then, 55% now. The mixed beings took it: CHAMPION (2) from
0% to 98%, gallery_4 from 50% to 100%. The hunter gallery_6, with no green
at all, now lives in 7 of 8 worlds and holds 71% of the living (3 of 8
and 0% before).

**The arena:**

| rank | first rules | water |
|---|---|---|
| 1 | ultimate, plant, 90% | gallery_4, mixed, 99% |
| 2 | gallery_7, mixed, 82% | seed 20908, mixed, 76% |
| 3 | seed 20908, mixed, 59% | CHAMPION (2), mixed, 71% |
| 4 | gallery_4, mixed, 49% | gallery_8, mixed, 69% |
| 5 | seed 15420, plant, 46% | ultimate-syn1, plant, 60% |
| 6 | ARAXIRA sp, mixed, 43% | month_month, plant, 60% |
| best hunter | gallery_6, 7th, 40% | gallery_6, 11th, 43% |

The top six are 4 mixed and 2 plants under both rules. What changed is
who: the all-green ultimate fell from first to ninth (49%), and the
leading beings are now green bodies with red on them. None of 0.9's
hunters wins more than 43% of its rounds.
