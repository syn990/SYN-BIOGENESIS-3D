#!/usr/bin/env python3
# SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
# Copyright (C) 2026 syn990
# SPDX-License-Identifier: GPL-2.0-or-later (see LICENSE)

"""Who wins in the water? Runs the headless world over many seeds and
shapes, brings the library's 0.9 beings in, and fights them in a round-robin
arena, then writes docs/ecology/tables.md. Standard library only.

    tools/ecology.py run [worlds|beings|arena]   every run not already in
                                                 runs/ecology/ (resumable)
    tools/ecology.py report                      the tables, from the runs
    BIN=path tools/ecology.py ...                another build
    RULES=first BIN=path tools/ecology.py ...    the first rules (0.9's in a
                                                 box), with the build of that
                                                 time and its own worlds: see
                                                 docs/ecology/README.md

What is measured, for each world at its end (time 400):
  alive       anything still living;
  hunting     at least one bite, kill or infection a time unit over the
              last quarter;
  kinds       every living organism by its make-up: a plant (under a tenth
              of its length red, gray or white), mixed (that and at least
              40% green) or a hunter (less green);
  the winner  the founder whose line holds the most living length, and the
              kind that founder was at genesis, and the kind most of the
              world's living are now;
  depth       where each kind lives, below the surface.
"""

import csv
import itertools
import math
import os
import re
import statistics
import subprocess
import sys
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("BIN", ROOT / "syn-biogenesis-3d"))
FIRST = os.environ.get("RULES") == "first"
RUNS = ROOT / "runs" / ("ecology-first" if FIRST else "ecology")
OUT = ROOT / "docs" / "ecology"
TABLES = OUT / ("first-rules.md" if FIRST else "tables.md")
FRAMES_PER_UNIT = 256
COLOURS = ("green", "red", "cyan", "blue", "white", "gray", "yellow")
KINDS = ("plant", "mixed", "hunter")

UNITS, EVERY = 400, 50
WORLDS = {  # name: (w, h, d, seeds)
    "default": (600, 600, 300, range(1, 49)),
    "pond": (1000, 1000, 64, range(1, 25)),
    "cube": (400, 400, 400, range(1, 25)),
    "column": (300, 300, 900, range(1, 25)),
}
if FIRST:  # the first rules' own default world, and a cube of its volume
    WORLDS = {
        "default": (1000, 1000, 32, range(1, 49)),
        "cube": (318, 318, 317, range(1, 25)),
    }
BEING_UNITS, BEING_SEEDS, COPIES = 200, range(1, 9), 10
ARENA_SEEDS = range(1, 5)

LIBRARY = Path.home() / ".config" / "syn-biogenesis" / "beings" / "0.9"
GALLERY = ROOT.parent / "SYN-BIOGENESIS" / "tools" / "javaref" / "download" / "gallery"


def beings():
    found = []
    for sub in ("Arena", "Saved", "Imported", "Forged"):
        found += sorted((LIBRARY / sub).glob("*.bgg"))
    found += sorted(f for f in GALLERY.glob("*.bgg") if not f.name.endswith(".old.bgg"))
    return found


def being_tag(path):
    src = "gallery" if path.parent == GALLERY else path.parent.name.lower()
    return f"{src}: {path.stem}"


def make_up(lengths):
    """plant, mixed or hunter, from length by colour."""
    total = sum(lengths.get(c, 0) for c in COLOURS) or 1
    green = lengths.get("green", 0) / total
    hunt = sum(lengths.get(c, 0) for c in ("red", "gray", "white")) / total
    if hunt < 0.1:
        return "plant"
    return "mixed" if green >= 0.4 else "hunter"


def being_make_up(path):
    lengths = Counter()
    for m in re.finditer(r"<gene\b[^>]*>", path.read_text(errors="replace")):
        tag = m.group(0)
        length = re.search(r'length="([^"]+)"', tag)
        colour = re.search(r'color="([^"]+)"', tag)
        if length and colour:
            lengths[colour.group(1)] += float(length.group(1))
    return lengths


def describe(lengths):
    total = sum(lengths.values()) or 1
    top = sorted(((v / total, c) for c, v in lengths.items() if v), reverse=True)
    return ", ".join(f"{c} {round(100 * s)}%" for s, c in top[:3])


# ---- running ----

def world_jobs():
    for name, (w, h, d, seeds) in WORLDS.items():
        for seed in seeds:
            base = RUNS / "worlds" / f"{name}-s{seed}"
            yield base, ["--seed", seed, "--size", w, h, d, "--frames", UNITS * FRAMES_PER_UNIT,
                         "--every", EVERY, "--census", f"{base}.census.csv"]


def being_jobs():
    w, h, d, _ = WORLDS["default"]
    for i, being in enumerate(beings()):
        for mode, organisms in (("alone", 0), ("among", 30)):
            for seed in BEING_SEEDS:
                base = RUNS / "beings" / f"b{i:02d}-{mode}-s{seed}"
                yield base, ["--seed", seed, "--size", w, h, d, "--organisms", organisms,
                             "--import", being, "--copies", COPIES,
                             "--frames", BEING_UNITS * FRAMES_PER_UNIT, "--every", BEING_UNITS]


def arena_jobs():
    w, h, d, _ = WORLDS["default"]
    for (i, a), (j, b) in itertools.combinations(enumerate(beings()), 2):
        for seed in ARENA_SEEDS:
            base = RUNS / "arena" / f"b{i:02d}-b{j:02d}-s{seed}"
            yield base, ["--seed", seed, "--size", w, h, d, "--organisms", 0, "--copies", COPIES,
                         "--import", a, "--import", b,
                         "--frames", BEING_UNITS * FRAMES_PER_UNIT, "--every", BEING_UNITS]


def run_one(job):
    base, args = job
    out = base.with_suffix(".csv")
    if out.exists():
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = base.with_suffix(".tmp")
    with open(tmp, "w") as f:
        r = subprocess.run([str(BIN), "--headless"] + [str(a) for a in args], stdout=f,
                           stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        tmp.unlink()
        print(f"failed: {' '.join(map(str, args))}", file=sys.stderr)
        return 1
    tmp.rename(out)
    return 0


def run(which):
    sets = {"worlds": world_jobs, "beings": being_jobs, "arena": arena_jobs}
    todo = [job for name in (which or sets) for job in sets[name]()]
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        failed = sum(ex.map(run_one, todo))
    print(f"{len(todo)} runs, {failed} failed")


# ---- reading ----

def load(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def world_summary(base, depth):
    rows = load(base.with_suffix(".csv"))
    census = load(f"{base}.census.csv")
    late = [r for r in rows if float(r["time"]) > UNITS * 0.75]
    span = UNITS / 4
    hunting = sum(float(r["bites"]) + float(r["kills"]) + float(r["infections"])
                  for r in late) / span
    founders = {r["id"]: make_up({c: float(r[c]) for c in COLOURS})
                for r in census if r["time"] == "0"}
    living = [r for r in census if int(r["time"]) == UNITS and r["alive"] == "1"]
    s = {"alive": bool(living), "hunting": bool(living) and hunting >= 1, "pop": len(living)}
    if not living:
        return s
    by_kind, line_len, depths = Counter(), Counter(), defaultdict(list)
    for r in living:
        lengths = {c: float(r[c]) for c in COLOURS}
        k = make_up(lengths)
        size = sum(lengths.values())
        by_kind[k] += size
        line_len[r["founder"]] += size
        depths[k].append(depth - float(r["z"]))
    total = sum(by_kind.values())
    s["share"] = {k: by_kind[k] / total for k in KINDS}
    s["now"] = Counter(make_up({c: float(r[c]) for c in COLOURS})
                       for r in living).most_common(1)[0][0]
    s["founder"] = founders.get(line_len.most_common(1)[0][0], "?")
    s["lines"] = len(line_len)
    s["depth"] = {k: statistics.median(v) for k, v in depths.items()}
    s["level"] = {k: statistics.mean(float(r["level"]) for r in living
                                     if make_up({c: float(r[c]) for c in COLOURS}) == k)
                  for k in depths}
    s["flat"] = {k: statistics.mean(int(r["flat"]) for r in living
                                    if make_up({c: float(r[c]) for c in COLOURS}) == k)
                 for k in depths}
    return s


def med(xs):
    xs = [x for x in xs if x is not None and not math.isnan(x)]
    return statistics.median(xs) if xs else float("nan")


def pct(x):
    return "-" if math.isnan(x) else f"{100 * x:.0f}%"


def counts(c, n):
    return " / ".join(f"{c.get(k, 0)}" for k in KINDS) + (f" ({n - sum(c.values())} dead)"
                                                          if n - sum(c.values()) else "")


def report():
    OUT.mkdir(parents=True, exist_ok=True)
    md = ["<!-- written by tools/ecology.py report -->", ""]

    md += ["## Worlds", "",
           "| world | worlds | alive | hunting lasts | organisms | living length: plant / mixed / hunter "
           "| founders who won: plant / mixed / hunter | most living now: plant / mixed / hunter "
           "| depth: plant / mixed / hunter |",
           "|---|---|---|---|---|---|---|---|---|"]
    for name, (w, h, d, seeds) in WORLDS.items():
        runs = [world_summary(RUNS / "worlds" / f"{name}-s{s}", d) for s in seeds
                if (RUNS / "worlds" / f"{name}-s{s}.csv").exists()]
        if not runs:
            continue
        live = [r for r in runs if r["alive"]]
        md.append(
            f"| {name}, {w} x {h} x {d} | {len(runs)} | {len(live)} "
            f"| {sum(r['hunting'] for r in runs)} "
            f"| {med([r['pop'] for r in live]):.0f} "
            f"| " + " / ".join(pct(med([r['share'][k] for r in live])) for k in KINDS) +
            f" | {counts(Counter(r['founder'] for r in live), len(runs))} "
            f"| {counts(Counter(r['now'] for r in live), len(runs))} "
            f"| " + " / ".join(f"{med([r['depth'].get(k) for r in live]):.0f}" for k in KINDS) + " |")
    md.append("")
    md += ["## How bodies lie", "",
           "| world | level: plant / mixed / hunter | flat codes: plant / mixed / hunter |",
           "|---|---|---|"]
    for name, (w, h, d, seeds) in WORLDS.items():
        runs = [world_summary(RUNS / "worlds" / f"{name}-s{s}", d) for s in seeds
                if (RUNS / "worlds" / f"{name}-s{s}.csv").exists()]
        live = [r for r in runs if r["alive"]]
        if not live:
            continue
        md.append(f"| {name} | " +
                  " / ".join(f"{med([r['level'].get(k) for r in live]):.2f}" for k in KINDS) +
                  " | " + " / ".join(pct(med([r['flat'].get(k) for r in live])) for k in KINDS) +
                  " |")
    md.append("")

    all_beings = beings()
    if (RUNS / "beings").exists():
        md += ["## 0.9's beings in the default world", "",
               "Alone: in how many of 8 worlds its line lives at time 200, and how many it "
               "numbers then (median). Among: the same among 30 random founders, and its share "
               "of the living.", "",
               "| being | make-up | kind | alone | among |", "|---|---|---|---|---|"]
        for i, being in enumerate(all_beings):
            lengths = being_make_up(being)
            cells = []
            for mode in ("alone", "among"):
                ends = []
                for s in BEING_SEEDS:
                    p = RUNS / "beings" / f"b{i:02d}-{mode}-s{s}.csv"
                    if p.exists():
                        ends.append(load(p)[-1])
                if not ends:
                    cells.append("-")
                    continue
                kept = [int(e["marked"]) for e in ends]
                if mode == "alone":
                    cells.append(f"{sum(k > 0 for k in kept)}/{len(ends)}, {med(kept):.0f}")
                else:
                    share = [int(e["marked"]) / int(e["population"]) if int(e["population"]) else 0
                             for e in ends]
                    cells.append(f"{sum(k > 0 for k in kept)}/{len(ends)}, {pct(med(share))}")
            md.append(f"| {being_tag(being)} | {describe(lengths)} | {make_up(lengths)} | "
                      + " | ".join(cells) + " |")
        md.append("")

    if (RUNS / "arena").exists():
        wins, rounds = Counter(), Counter()
        for (i, a), (j, b) in itertools.combinations(enumerate(all_beings), 2):
            for s in ARENA_SEEDS:
                p = RUNS / "arena" / f"b{i:02d}-b{j:02d}-s{s}.csv"
                if not p.exists():
                    continue
                end = load(p)[-1]
                la, lb = int(end["marked"]), int(end["line2"])
                rounds[i] += 1
                rounds[j] += 1
                if la > lb:
                    wins[i] += 1
                elif lb > la:
                    wins[j] += 1
        if rounds:
            order = sorted(rounds, key=lambda k: -wins[k] / rounds[k])
            md += ["## The arena", "",
                   f"Every pair of beings, {len(ARENA_SEEDS)} rounds each, 10 copies of each "
                   "and nothing else in the default world, 200 time units. A round goes to the "
                   "line with more living at the end.", "",
                   "| rank | being | kind | rounds won |", "|---|---|---|---|"]
            for rank, k in enumerate(order, 1):
                lengths = being_make_up(all_beings[k])
                md.append(f"| {rank} | {being_tag(all_beings[k])} | {make_up(lengths)} "
                          f"| {wins[k]} of {rounds[k]} ({pct(wins[k] / rounds[k])}) |")
            top = Counter(make_up(being_make_up(all_beings[k])) for k in order[:6])
            md += ["", "The top six by kind: " +
                   ", ".join(f"{top.get(k, 0)} {k}" for k in KINDS) + ".", ""]

    TABLES.write_text("\n".join(md) + "\n")
    print(f"wrote {TABLES}")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in ("run", "report"):
        print(__doc__)
        sys.exit(2)
    if sys.argv[1] == "run":
        run(sys.argv[2:])
    else:
        report()
