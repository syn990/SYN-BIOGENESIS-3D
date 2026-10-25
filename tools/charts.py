#!/usr/bin/env python3
# SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
# Copyright (C) 2026 syn990
# SPDX-License-Identifier: GPL-2.0-or-later (see LICENSE)

"""The README's charts. The same 24 seeds run to time 400 in Biogenesis 0.9
on its plane (by way of SYN-BIOGENESIS, which computes 0.9's worlds bit for
bit) and in the water, then: what the living are made of in each, where the
water's living keep, and how their bodies stand. Six of 0.9's beings are
brought into the water as tools/ecology.py brings them, alone, among random
founders and against each other. Writes docs/charts/*.svg and
docs/charts/tables.md. Standard library only.

    tools/charts.py run       every run not already in runs/charts/ (resumable)
    tools/charts.py report    the charts and tables, from the runs
    BIN=path BIN2D=path tools/charts.py ...   other builds (BIN2D: SYN-BIOGENESIS's)

Each organism alive is a plant (under a tenth of its length red, gray or
white), mixed (that, and at least 40% of it green or purple) or a hunter
(less), as docs/ecology classes them, with purple, which lives on methane,
counted beside green.
"""

import csv
import itertools
import math
import os
import statistics
import struct
import subprocess
import sys
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from ecology import GALLERY, LIBRARY, being_make_up, describe

ROOT = Path(__file__).resolve().parents[1]
BIN = Path(os.environ.get("BIN", ROOT / "syn-biogenesis-3d"))
BIN2D = Path(os.environ.get("BIN2D", ROOT.parent / "SYN-BIOGENESIS" / "syn-biogenesis"))
RUNS = ROOT / "runs" / "charts"
OUT = ROOT / "docs" / "charts"
FRAMES_PER_UNIT = 256
SEEDS = range(1, 25)
UNITS, EVERY = 400, 50
TIMES = list(range(0, UNITS + 1, EVERY))
DEPTH, LIGHT_DEPTH, BAND = 300, 250, 30  # the default box, its light, a census band
COLOURS = ("green", "red", "cyan", "blue", "white", "gray", "yellow", "purple")
COLOURS_2D = ("green", "red", "cyan", "blue", "white", "gray", "yellow", "brown")  # BioColor
KINDS = ("plant", "mixed", "hunter")
PLANS = ("disk", "cone", "orb")
BEINGS = [GALLERY / "gallery_4.bgg", LIBRARY / "Forged" / "seed 20908.bgg",
          LIBRARY / "Saved" / "CHAMPION (2).bgg", LIBRARY / "Imported" / "ultimate.bgg",
          GALLERY / "gallery_6.bgg", LIBRARY / "Saved" / "#305.bgg"]
BEING_UNITS, BEING_SEEDS, ARENA_SEEDS, COPIES = 200, range(1, 9), range(1, 5), 10


def make_up(lengths):
    """plant, mixed or hunter, from length by colour."""
    total = sum(lengths.values()) or 1
    own = (lengths.get("green", 0) + lengths.get("purple", 0)) / total
    hunt = sum(lengths.get(c, 0) for c in ("red", "gray", "white")) / total
    if hunt < 0.1:
        return "plant"
    return "mixed" if own >= 0.4 else "hunter"


# ---- running ----

def jobs():
    for seed in SEEDS:
        base = RUNS / f"3d-s{seed}"
        yield base.with_suffix(".csv"), [BIN, "--headless", "--seed", seed,
                                         "--frames", UNITS * FRAMES_PER_UNIT, "--every", EVERY,
                                         "--census", f"{base}.census.csv",
                                         "--profile", f"{base}.profile.csv"]
        for t in TIMES:
            out = RUNS / f"2d-s{seed}-t{t}.sbw"
            yield out, [BIN2D, "--headless", "--defaults", "--seed", seed,
                        "--frames", t * FRAMES_PER_UNIT, "--save", out, "--quiet"]
    beings = [(i, b) for i, b in enumerate(BEINGS) if b.exists()]
    for i, b in beings:
        for mode, organisms in (("alone", 0), ("among", 30)):
            for seed in BEING_SEEDS:
                yield RUNS / f"being{i}-{mode}-s{seed}.csv", [
                    BIN, "--headless", "--seed", seed, "--organisms", organisms, "--import", b,
                    "--copies", COPIES, "--frames", BEING_UNITS * FRAMES_PER_UNIT, "--every", BEING_UNITS]
    for (i, a), (j, b) in itertools.combinations(beings, 2):
        for seed in ARENA_SEEDS:
            yield RUNS / f"duel{i}-{j}-s{seed}.csv", [
                BIN, "--headless", "--seed", seed, "--organisms", 0, "--import", a, "--import", b,
                "--copies", COPIES, "--frames", BEING_UNITS * FRAMES_PER_UNIT, "--every", BEING_UNITS]


def run_one(job):
    out, args = job
    if out.exists():
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(".tmp")
    args = [str(a) for a in args]
    if out.suffix == ".sbw":
        args[args.index(str(out))] = str(tmp)
        r = subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        with open(tmp, "w") as f:
            r = subprocess.run(args, stdout=f, stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        tmp.unlink(missing_ok=True)
        print(f"failed: {' '.join(args)}", file=sys.stderr)
        return 1
    tmp.rename(out)
    return 0


def run():
    todo = list(jobs())
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        failed = sum(ex.map(run_one, todo))
    print(f"{len(todo)} runs, {failed} failed")


# ---- reading ----

def load(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def read_sbw(path):
    """The living of a SYN-BIOGENESIS world (.sbw, its src/io/worldfile.c):
    each one's segment length by colour."""
    b = path.read_bytes()
    pos = 0

    def take(fmt):
        nonlocal pos
        v = struct.unpack_from("<" + fmt, b, pos)
        pos += struct.calcsize("<" + fmt)
        return v

    if b[:8] != b"SYNBIOW\0":
        raise ValueError(f"{path}: not a SYN-BIOGENESIS world")
    pos = 8
    take("i iiiii dd")
    (ncodes,) = take("i")
    for _ in range(ncodes):
        *_, ngenes = take("ii B iii")
        take("ddB" * ngenes)
    take("q i i i q i q i i i q q i q i q d q d q d q d q i i i i q i q i q")
    take("9i")
    for _ in range(5):
        (n,) = take("i")
        take(f"{n}d")
    (live,) = take("i")
    living = []
    for _ in range(live):
        (_, _, _, _, _, _, _, _, _, _, _, _, _, _, segments, *_) = take("ii iiii iiiiiiii i ii dd i ddd iiii ddddd i B i i i B B")
        alive = b[pos - 1]
        lengths = Counter()
        for _ in range(segments):
            *_, m, _, _, _, _, colour = take("iiii ddd iiii B")
            lengths[COLOURS_2D[colour]] += m
        if alive:
            living.append(lengths)
    return living


def shares(living):
    """each kind's share of the living length; None when nothing lives"""
    by_kind = Counter()
    for lengths in living:
        by_kind[make_up(lengths)] += sum(lengths.values())
    total = sum(by_kind.values())
    return {k: by_kind[k] / total for k in KINDS} if total else None


def gather():
    g = {"2d": defaultdict(list), "3d": defaultdict(list), "pop2d": defaultdict(list),
         "pop3d": defaultdict(list), "plans": defaultdict(list), "flat": defaultdict(list),
         "bands": {k: Counter() for k in KINDS}, "level": defaultdict(list),
         "light": defaultdict(list), "hunting": 0,
         "alive2d": 0, "alive3d": 0}
    for seed in SEEDS:
        for t in TIMES:
            living = read_sbw(RUNS / f"2d-s{seed}-t{t}.sbw")
            g["pop2d"][t].append(len(living))
            s = shares(living)
            if s:
                g["2d"][t].append(s)
            if t == UNITS and living:
                g["alive2d"] += 1
        base = RUNS / f"3d-s{seed}"
        census = defaultdict(list)
        for r in load(f"{base}.census.csv"):
            if r["alive"] == "1":
                census[int(r["time"])].append(r)
        for t in TIMES:
            rows = census[t]
            g["pop3d"][t].append(len(rows))
            s = shares([{c: float(r[c]) for c in COLOURS} for r in rows])
            if s:
                g["3d"][t].append(s)
                g["plans"][t].append({p: sum(r["plan"] == p for r in rows) / len(rows) for p in PLANS})
                g["flat"][t].append(sum(r["flat"] == "1" for r in rows) / len(rows))
            if t >= UNITS * 3 // 4:
                for r in rows:
                    band = min(int((DEPTH - float(r["z"])) // BAND), DEPTH // BAND - 1)
                    k = make_up({c: float(r[c]) for c in COLOURS})
                    g["bands"][k][band] += 1
                    g["level"][k].append(float(r["level"]))
        if census[UNITS]:
            g["alive3d"] += 1
        rows = load(base.with_suffix(".csv"))
        late = [r for r in rows if float(r["time"]) > UNITS * 3 / 4]
        bites = sum(float(r["bites"]) + float(r["kills"]) + float(r["infections"]) for r in late)
        g["hunting"] += bool(census[UNITS]) and bites / (UNITS / 4) >= 1
        for r in load(f"{base}.profile.csv"):
            if int(r["time"]) >= UNITS * 3 // 4:
                g["light"][int(float(r["depth"]) // BAND)].append(float(r["light"]))
    return g


def beings_summary():
    """each being: alone and among random founders, and its duels"""
    present = [(i, b) for i, b in enumerate(BEINGS) if b.exists()]
    end = lambda path: load(path)[-1]
    wins, fought = Counter(), Counter()
    for (i, a), (j, b) in itertools.combinations(present, 2):
        for seed in ARENA_SEEDS:
            r = end(RUNS / f"duel{i}-{j}-s{seed}.csv")
            fought[i] += 1
            fought[j] += 1
            x, y = int(r["marked"]), int(r["line2"])
            if x != y:
                wins[i if x > y else j] += 1
    rows = []
    for i, b in present:
        out = {}
        for mode in ("alone", "among"):
            ends = [end(RUNS / f"being{i}-{mode}-s{seed}.csv") for seed in BEING_SEEDS]
            out[mode] = (sum(int(r["marked"]) > 0 for r in ends),
                         statistics.median(int(r["marked"]) for r in ends),
                         statistics.median(int(r["marked"]) / int(r["population"]) if int(r["population"]) else 0
                                           for r in ends))
        lengths = being_make_up(b)
        rows.append((b, describe(lengths), make_up(lengths), out, wins[i], fought[i]))
    return rows


def mean_share(per_world, key):
    return statistics.mean(s[key] for s in per_world) if per_world else float("nan")


# ---- charts (the sweep's look: a light and a dark SVG of each) ----

THEMES = {
    "light": {"surface": "#fcfcfb", "ink": "#0b0b0b", "ink2": "#52514e", "muted": "#898781",
              "grid": "#e1e0d9", "axis": "#c3c2b7", "border": "rgba(11,11,11,0.10)",
              "series": ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]},
    "dark": {"surface": "#1a1a19", "ink": "#f0efec", "ink2": "#c3c2b7", "muted": "#898781",
             "grid": "#2c2c2a", "axis": "#383835", "border": "rgba(255,255,255,0.10)",
             "series": ["#3987e5", "#d95926", "#199e70", "#c98500"]},
}
KIND_SLOT = {"plant": 2, "mixed": 0, "hunter": 1}  # aqua, blue, orange
KIND_NAME = {"plant": "plants", "mixed": "mixed", "hunter": "hunters"}
FONT = "system-ui, -apple-system, 'Segoe UI', sans-serif"
CW = 820


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def pct(v):
    return "-" if math.isnan(v) else f"{100 * v:.0f}%"


def frame(t, h, title, subtitle):
    return [f'<svg xmlns="http://www.w3.org/2000/svg" width="{CW}" height="{h}" '
            f'viewBox="0 0 {CW} {h}" font-family="{FONT}">',
            f'<title>{esc(title)}</title>',
            f'<rect x="0.5" y="0.5" width="{CW - 1}" height="{h - 1}" rx="8" '
            f'fill="{t["surface"]}" stroke="{t["border"]}"/>',
            f'<text x="24" y="34" font-size="16" font-weight="600" fill="{t["ink"]}">{esc(title)}</text>',
            f'<text x="24" y="56" font-size="13" fill="{t["ink2"]}">{esc(subtitle)}</text>']


def legend(p, t, y, items):
    """items: (label, colour, dashed)"""
    x = 24
    for label, c, dashed in items:
        d = ' stroke-dasharray="5 4"' if dashed else ""
        p.append(f'<line x1="{x}" y1="{y}" x2="{x + 18}" y2="{y}" stroke="{c}" '
                 f'stroke-width="2" stroke-linecap="round"{d}/>')
        p.append(f'<text x="{x + 24}" y="{y + 4}" font-size="12" fill="{t["ink2"]}">{esc(label)}</text>')
        x += 24 + 7.1 * len(label) + 22


def lines_panel(p, t, x0, y0, w, h, head, series, xmax=UNITS):
    """series: (label, colour, dashed, [(time, share)]); 0 to 100% up the side"""
    sx = lambda v: x0 + v / xmax * w
    sy = lambda v: y0 + h - v * h
    p.append(f'<text x="{x0}" y="{y0 - 14}" font-size="13" font-weight="600" fill="{t["ink"]}">{esc(head)}</text>')
    for i in range(5):
        y = sy(i / 4)
        p.append(f'<line x1="{x0}" y1="{y:.1f}" x2="{x0 + w}" y2="{y:.1f}" stroke="{t["grid"]}"/>')
        p.append(f'<text x="{x0 - 6}" y="{y + 4:.1f}" font-size="11" fill="{t["muted"]}" '
                 f'text-anchor="end">{25 * i}%</text>')
    for v in range(0, xmax + 1, 100):
        p.append(f'<text x="{sx(v):.1f}" y="{y0 + h + 16}" font-size="11" fill="{t["muted"]}" '
                 f'text-anchor="middle">{v}</text>')
    p.append(f'<line x1="{x0}" y1="{y0 + h}" x2="{x0 + w}" y2="{y0 + h}" stroke="{t["axis"]}"/>')
    ends = []
    for label, c, dashed, pts in series:
        pts = [(a, b) for a, b in pts if not math.isnan(b)]
        if not pts:
            continue
        d = ' stroke-dasharray="5 4"' if dashed else ""
        path = " ".join(f"{'M' if i == 0 else 'L'}{sx(a):.1f},{sy(b):.1f}" for i, (a, b) in enumerate(pts))
        p.append(f'<path d="{path}" fill="none" stroke="{c}" stroke-width="2" stroke-linejoin="round"{d}/>')
        a, b = pts[-1]
        p.append(f'<circle cx="{sx(a):.1f}" cy="{sy(b):.1f}" r="4" fill="{c}" stroke="{t["surface"]}" stroke-width="2"/>')
        ends.append([sy(b), label, b])
    # the end labels, kept 13px apart
    ends.sort()
    for i in range(1, len(ends)):
        ends[i][0] = max(ends[i][0], ends[i - 1][0] + 13)
    for y, label, v in ends:
        p.append(f'<text x="{x0 + w + 8}" y="{y + 4:.1f}" font-size="11" fill="{t["ink2"]}">'
                 f'{esc(label)} {pct(v)}</text>')


def chart_kinds(t, g, path):
    """What the living are made of at time 400: a bar each, 2D and 3D."""
    rows = [("Biogenesis 0.9, on its plane (1,000 x 1,000)", g["2d"][UNITS]),
            ("SYN-BIOGENESIS-3D, in the water (600 x 600 x 300)", g["3d"][UNITS])]
    h = 300
    p = frame(t, h, "What the living are made of, on the plane and in the water",
              "Share of the living length at time 400 by kind, mean over the living worlds of seeds 1 to 24")
    legend(p, t, 82, [(KIND_NAME[k], t["series"][KIND_SLOT[k]], False) for k in KINDS])
    x0, w = 24, CW - 48
    for i, (label, worlds) in enumerate(rows):
        y = 124 + i * 82
        p.append(f'<text x="{x0}" y="{y - 8}" font-size="13" fill="{t["ink"]}">'
                 f'{esc(label)} · {len(worlds)} worlds alive</text>')
        x = x0
        for k in KINDS:
            v = mean_share(worlds, k)
            seg = v * w
            if seg >= 1:
                p.append(f'<rect x="{x:.1f}" y="{y}" width="{max(seg - 2, 1):.1f}" height="26" rx="4" '
                         f'fill="{t["series"][KIND_SLOT[k]]}"/>')
                if seg >= 30:
                    text = f"{KIND_NAME[k]} {pct(v)}" if seg >= 90 else pct(v)
                    p.append(f'<text x="{x + seg / 2:.1f}" y="{y + 44}" font-size="12" fill="{t["ink2"]}" '
                             f'text-anchor="middle">{text}</text>')
            x += seg
    p.append("</svg>")
    path.write_text("\n".join(p) + "\n")


def chart_kinds_time(t, g, path):
    """Each kind's share over time, 2D dashed and 3D solid, a panel each."""
    h = 340
    p = frame(t, h, "How the plane and the water part over time",
              "Each kind's share of the living length every 50 time units, mean over the living worlds of seeds 1 to 24")
    legend(p, t, 82, [("in the water (3D)", t["ink2"], False), ("Biogenesis 0.9 (2D)", t["ink2"], True)])
    pw, gap = 168, 94
    for i, k in enumerate(KINDS):
        c = t["series"][KIND_SLOT[k]]
        series = [("3D", c, False, [(tm, mean_share(g["3d"][tm], k)) for tm in TIMES]),
                  ("2D", c, True, [(tm, mean_share(g["2d"][tm], k)) for tm in TIMES])]
        lines_panel(p, t, 64 + i * (pw + gap), 136, pw, 160, KIND_NAME[k].capitalize(), series)
    p.append("</svg>")
    path.write_text("\n".join(p) + "\n")


def chart_depth(t, g, path):
    """Where each kind keeps: its organisms by depth band, a panel each."""
    nb = DEPTH // BAND
    row, top = 22, 124
    h = top + nb * row + 52
    p = frame(t, h, "Where the water's living keep",
              "Each kind's organisms by depth, times 300 to 400, seeds 1 to 24; the light there as a share of the surface's")
    lx = 150
    p.append(f'<text x="{lx - 10}" y="{top - 14}" font-size="11" fill="{t["muted"]}" text-anchor="end">'
             f'depth · light</text>')
    for b in range(nb):
        y = top + b * row
        light = statistics.mean(g["light"][b]) if g["light"][b] else math.exp(-(b + 0.5) * BAND / LIGHT_DEPTH)
        p.append(f'<text x="{lx - 10}" y="{y + 15}" font-size="11" fill="{t["muted"]}" text-anchor="end">'
                 f'{b * BAND}–{(b + 1) * BAND} · {pct(light)}</text>')
    pw, gap = 176, 30
    for i, k in enumerate(KINDS):
        x0 = lx + i * (pw + gap)
        total = sum(g["bands"][k].values()) or 1
        vals = [g["bands"][k][b] / total for b in range(nb)]
        vmax = max(0.4, max(vals))
        p.append(f'<text x="{x0}" y="{top - 14}" font-size="13" font-weight="600" fill="{t["ink"]}">'
                 f'{esc(KIND_NAME[k].capitalize())}</text>')
        p.append(f'<line x1="{x0}" y1="{top - 4}" x2="{x0}" y2="{top + nb * row}" stroke="{t["axis"]}"/>')
        best = max(range(nb), key=lambda b: vals[b])
        for b, v in enumerate(vals):
            y = top + b * row
            bw = v / vmax * (pw - 40)
            if bw >= 1:
                p.append(f'<rect x="{x0}" y="{y + 3}" width="{bw:.1f}" height="{row - 6}" rx="4" '
                         f'fill="{t["series"][KIND_SLOT[k]]}"/>')
            if b == best or b == 0:
                p.append(f'<text x="{x0 + bw + 6:.1f}" y="{y + 15}" font-size="11" fill="{t["ink2"]}">'
                         f'{pct(v)}</text>')
        p.append(f'<text x="{x0}" y="{top + nb * row + 18}" font-size="11" fill="{t["muted"]}">'
                 f'{total:,} organisms</text>')
    p.append(f'<text x="{CW - 24}" y="{h - 14}" font-size="11" fill="{t["muted"]}" text-anchor="end">'
             f'the surface at the top; the floor 300 down</text>')
    p.append("</svg>")
    path.write_text("\n".join(p) + "\n")


def chart_plans(t, g, path):
    """How bodies stand over time: their plans, and how many stay flat."""
    h = 340
    p = frame(t, h, "How the water's bodies stand",
              "Share of the living organisms every 50 time units, mean over the living worlds of seeds 1 to 24")
    series = [(name, t["series"][slot], False,
               [(tm, statistics.mean(s[name] for s in g["plans"][tm]) if g["plans"][tm] else float("nan"))
                for tm in TIMES])
              for slot, name in enumerate(PLANS)]
    legend(p, t, 82, [(f"{name}: {what}", c, False) for (name, c, _, _), what
                      in zip(series, ("arms in a plane, as 0.9's", "arms tilted out of it", "arms over a sphere"))])
    lines_panel(p, t, 64, 136, 370, 160, "Body plans", series)
    flat = [("flat", t["ink2"], False,
             [(tm, statistics.mean(g["flat"][tm]) if g["flat"][tm] else float("nan")) for tm in TIMES])]
    lines_panel(p, t, 540, 136, 200, 160, "Every gene flat, as 0.9's", flat)
    p.append("</svg>")
    path.write_text("\n".join(p) + "\n")


def report():
    g = gather()
    OUT.mkdir(parents=True, exist_ok=True)
    for theme in THEMES:
        t = THEMES[theme]
        chart_kinds(t, g, OUT / f"kinds-{theme}.svg")
        chart_kinds_time(t, g, OUT / f"kinds-time-{theme}.svg")
        chart_depth(t, g, OUT / f"depth-{theme}.svg")
        chart_plans(t, g, OUT / f"plans-{theme}.svg")

    md = ["<!-- written by tools/charts.py report -->", "",
          "## The plane and the water", "",
          "Seeds 1 to 24, each in Biogenesis 0.9 (SYN-BIOGENESIS, 0.9's defaults: 1,000 x 1,000, "
          "15 founders, 5,000 CO2) and in the water's default box (600 x 600 x 300, 30 founders). "
          "Shares are means over the living worlds; organisms are medians over all 24.", "",
          "| time | organisms: 2D / 3D | plants: 2D / 3D | mixed: 2D / 3D | hunters: 2D / 3D |",
          "|---|---|---|---|---|"]
    for tm in TIMES:
        md.append(f"| {tm} | {statistics.median(g['pop2d'][tm]):.0f} / {statistics.median(g['pop3d'][tm]):.0f} | "
                  + " | ".join(f"{pct(mean_share(g['2d'][tm], k))} / {pct(mean_share(g['3d'][tm], k))}"
                               for k in KINDS) + " |")
    md += ["", f"Worlds alive at time {UNITS}: {g['alive2d']} of {len(SEEDS)} on the plane, "
               f"{g['alive3d']} of {len(SEEDS)} in the water; hunting lasts (a bite, kill or infection a "
               f"time unit over the last quarter) in {g['hunting']} of the water's.", "",
           "## Where the water's living keep", "",
           "Each kind's organisms by depth band, times 300, 350 and 400 together; the light is the "
           "water's mean there, shade included, as a share of the surface's.", "",
           "| depth | light | " + " | ".join(KIND_NAME[k] for k in KINDS) + " |",
           "|---|---|---|---|---|"]
    for b in range(DEPTH // BAND):
        light = statistics.mean(g["light"][b]) if g["light"][b] else float("nan")
        md.append(f"| {b * BAND}–{(b + 1) * BAND} | {pct(light)} | "
                  + " | ".join(pct(g["bands"][k][b] / (sum(g["bands"][k].values()) or 1)) for k in KINDS) + " |")
    md += ["", "| | " + " | ".join(KIND_NAME[k] for k in KINDS) + " |", "|---|---|---|---|",
           "| organisms counted | " + " | ".join(f"{sum(g['bands'][k].values()):,}" for k in KINDS) + " |",
           "| how level they lie (1 level, 0.5 at random) | "
           + " | ".join(f"{statistics.mean(g['level'][k]):.2f}" if g["level"][k] else "-" for k in KINDS) + " |",
           "", "## How the water's bodies stand", "",
           "| time | disk | cone | orb | every gene flat |", "|---|---|---|---|---|"]
    for tm in TIMES:
        pl = g["plans"][tm]
        md.append(f"| {tm} | " + " | ".join(pct(statistics.mean(s[n] for s in pl)) if pl else "-" for n in PLANS)
                  + f" | {pct(statistics.mean(g['flat'][tm])) if g['flat'][tm] else '-'} |")
    md += ["", "## Six of 0.9's beings in the water", "",
           f"Each arrives flat, with the drives and height its body asks for, {COPIES} copies, "
           f"{BEING_UNITS} time units in the default box, seeds {BEING_SEEDS.start} to {BEING_SEEDS.stop - 1}. "
           "Alone: in how many worlds its line lives at the end, and how many it numbers then (median). "
           "Among: the same among 30 random founders, with its share of the living (median). "
           f"Duels: {COPIES} copies each of two beings and nothing else, seeds {ARENA_SEEDS.start} to "
           f"{ARENA_SEEDS.stop - 1}; a duel goes to the line with more living at the end.", "",
           "| being | make-up | kind | alone | among | duels won |", "|---|---|---|---|---|---|"]
    for b, mk, kind, o, won, fought in beings_summary():
        n = len(BEING_SEEDS)
        md.append(f"| {b.stem} | {mk} | {kind} | {o['alone'][0]}/{n}, {o['alone'][1]:.0f} | "
                  f"{o['among'][0]}/{n}, {pct(o['among'][2])} | {won} of {fought} |")
    (OUT / "tables.md").write_text("\n".join(md) + "\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "run":
        run()
    elif cmd == "report":
        report()
    else:
        print(__doc__.strip())
        sys.exit(2)
