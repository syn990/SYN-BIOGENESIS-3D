#!/usr/bin/env python3
# SYN-BIOGENESIS-3D: Biogenesis in three dimensions.
# Copyright (C) 2026 syn990
# SPDX-License-Identifier: GPL-2.0-or-later (see LICENSE)

"""Does 0.9's life cope with a third dimension? Runs the headless world in
boxes from a slab one body thick to a cube, and brings 0.9 beings in flat,
then writes docs/sweep/README.md's tables and charts. Standard library only.

    tools/sweep.py run       # every run not already in runs/ (resumable)
    tools/sweep.py report    # tables and charts from runs/

Two series of worlds, each with flat new genes (pitch 0, life as 0.9 makes
it) and with half of all new genes bent (pitch 50):
  floor   the 1000 x 1000 floor of 0.9's default world, deepened from 4 to
          1000: the same carbon spread through ever more water;
  volume  the default world's volume (1000 x 1000 x 32) kept, reshaped
          from that slab to a cube 318 wide: the same water, another shape.
"""

import csv
import math
import os
import statistics
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "syn-biogenesis-3d"
RUNS = ROOT / "runs"
OUT = ROOT / "docs" / "sweep"
FRAMES_PER_UNIT = 256

SWEEP_UNITS, SWEEP_EVERY, SWEEP_SEEDS = 400, 4, range(1, 25)
BEING_UNITS, BEING_EVERY, BEING_SEEDS, BEING_COPIES = 200, 4, range(1, 9), 10
FLOOR_DEPTHS = (4, 8, 16, 32, 64, 128, 256, 512, 1000)
VOLUME = 1000 * 1000 * 32
VOLUME_DEPTHS = (32, 64, 128, 317)
PITCHES = (0, 50)

# the beings: the user's library and the original website's gallery
LIBRARY = Path.home() / ".config" / "syn-biogenesis" / "beings" / "0.9"
GALLERY = ROOT.parent / "SYN-BIOGENESIS" / "tools" / "javaref" / "download" / "gallery"
SHAPES = {  # name: (w, h, d, pitch)
    "slab": (1000, 1000, 4, 0),
    "default": (1000, 1000, 32, 50),
    "cube": (318, 318, 317, 50),
}


def sweep_configs():
    for d in FLOOR_DEPTHS:
        for p in PITCHES:
            yield ("floor", 1000, 1000, d, p)
    for d in VOLUME_DEPTHS:
        s = round(math.sqrt(VOLUME / d))
        for p in PITCHES:
            yield ("volume", s, s, d, p)


def run_path(w, h, d, p, seed, extra=""):
    return RUNS / f"{w}x{h}x{d}-p{p}{extra}-s{seed}.csv"


def beings():
    found = []
    for sub in ("Arena", "Saved", "Imported", "Forged"):
        found += sorted((LIBRARY / sub).glob("*.bgg"))
    found += sorted(f for f in GALLERY.glob("*.bgg") if not f.name.endswith(".old.bgg"))
    return found


def being_tag(path):
    src = "gallery" if path.parent == GALLERY else path.parent.name.lower()
    return f"{src}:{path.stem}"


def jobs():
    for _, w, h, d, p in sweep_configs():
        for seed in SWEEP_SEEDS:
            out = run_path(w, h, d, p, seed)
            args = ["--seed", seed, "--frames", SWEEP_UNITS * FRAMES_PER_UNIT, "--size", w, h, d,
                    "--pitch", p, "--every", SWEEP_EVERY]
            yield out, args
    for i, being in enumerate(beings()):
        for shape, (w, h, d, p) in SHAPES.items():
            for mode, organisms in (("alone", 0), ("among", 15)):
                for seed in BEING_SEEDS:
                    out = RUNS / "beings" / f"b{i:02d}-{shape}-{mode}-s{seed}.csv"
                    args = ["--seed", seed, "--frames", BEING_UNITS * FRAMES_PER_UNIT,
                            "--size", w, h, d, "--pitch", p, "--organisms", organisms,
                            "--import", being, "--copies", BEING_COPIES,
                            "--every", BEING_EVERY]
                    yield out, args


def run_one(job):
    out, args = job
    if out.exists():
        return 0
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_suffix(".tmp")
    with open(tmp, "w") as f:
        r = subprocess.run([str(BIN)] + [str(a) for a in args], stdout=f,
                           stderr=subprocess.DEVNULL)
    if r.returncode != 0:
        tmp.unlink()
        print(f"failed: {' '.join(map(str, args))}", file=sys.stderr)
        return 1
    tmp.rename(out)
    return 0


def run():
    todo = list({out: (out, args) for out, args in jobs()}.values())
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
        failed = sum(ex.map(run_one, todo))
    print(f"{len(todo)} runs, {failed} failed")


# ---- reading runs ----

COLOURS = ("green", "red", "cyan", "blue", "white", "gray", "yellow")


def load(path):
    with open(path) as f:
        return [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]


def summarise(rows, units, every):
    """What one run looked like over its last quarter."""
    late = [r for r in rows if r["time"] > units * 0.75]
    end = rows[-1]
    living = [r for r in late if r["population"] > 0]
    share = lambda keys: statistics.mean(
        sum(r[k] for k in keys) / max(1e-9, sum(r[c] for c in COLOURS)) for r in living
    ) if living else float("nan")
    span = len(late) * every
    hunting = sum(r["bites"] + r["kills"] + r["infections"] for r in late) / span
    return {
        "alive": end["population"] > 0,
        # hunting is all or nothing in a world: a predator line either holds
        # (tens to hundreds of bites a unit) or there is next to none
        "hunting": end["population"] > 0 and hunting >= 1,
        "pop": statistics.mean(r["population"] for r in late),
        "green": share(("green",)),
        "hunters": share(("red", "gray", "white")),
        "kills": sum(r["kills"] for r in late) / span,
        "infections": sum(r["infections"] for r in late) / span,
        "bites": sum(r["bites"] for r in late) / span,
        "eaten": sum(r["eaten"] for r in late) / span,
        "flat": end["flat"] if end["population"] > 0 else float("nan"),
        "level": statistics.mean(r["level"] for r in living) if living else float("nan"),
        "segments": statistics.mean(r["mean_segments"] for r in living) if living else float("nan"),
        "marked": end["marked"],
        "marked_share": end["marked"] / end["population"] if end["population"] > 0 else 0.0,
    }


def med(xs):
    xs = [x for x in xs if not math.isnan(x)]
    return statistics.median(xs) if xs else float("nan")


def fmt(x, digits=1):
    if math.isnan(x):
        return "-"
    return f"{x:.{digits}f}"


def pct(x):
    return "-" if math.isnan(x) else f"{100 * x:.0f}%"


# ---- charts (the drift study's look: a light and a dark SVG of each) ----

THEMES = {
    "light": {"surface": "#fcfcfb", "ink": "#0b0b0b", "ink2": "#52514e", "muted": "#898781",
              "grid": "#e1e0d9", "axis": "#c3c2b7", "border": "rgba(11,11,11,0.10)",
              "series": ["#2a78d6", "#eb6834", "#1baf7a", "#eda100"]},
    "dark": {"surface": "#1a1a19", "ink": "#f0efec", "ink2": "#c3c2b7", "muted": "#898781",
             "grid": "#2c2c2a", "axis": "#383835", "border": "rgba(255,255,255,0.10)",
             "series": ["#3987e5", "#d95926", "#199e70", "#c98500"]},
}
FONT = "system-ui, -apple-system, 'Segoe UI', sans-serif"
CW, CH = 820, 440
CL, CR, CT, CB = 64, 24, 96, 48


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def chart(theme, title, subtitle, series, ymax, yfmt, path):
    """series: (label, slot, dashed, [(depth, value)]) on a log depth axis."""
    t = THEMES[theme]
    p = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{CW}" height="{CH}" '
         f'viewBox="0 0 {CW} {CH}" font-family="{FONT}">',
         f'<title>{esc(title)}</title>',
         f'<rect x="0.5" y="0.5" width="{CW - 1}" height="{CH - 1}" rx="8" '
         f'fill="{t["surface"]}" stroke="{t["border"]}"/>',
         f'<text x="24" y="34" font-size="16" font-weight="600" fill="{t["ink"]}">{esc(title)}</text>',
         f'<text x="24" y="56" font-size="13" fill="{t["ink2"]}">{esc(subtitle)}</text>']
    x = 24
    for label, slot, dashed, _ in series:
        d = ' stroke-dasharray="5 4"' if dashed else ""
        p.append(f'<line x1="{x}" y1="74" x2="{x + 18}" y2="74" stroke="{t["series"][slot]}" '
                 f'stroke-width="2" stroke-linecap="round"{d}/>')
        p.append(f'<text x="{x + 24}" y="78" font-size="12" fill="{t["ink2"]}">{esc(label)}</text>')
        x += 24 + 7.1 * len(label) + 22
    lx0, lx1 = math.log(4), math.log(1000)
    sx = lambda d: CL + (math.log(d) - lx0) / (lx1 - lx0) * (CW - CL - CR)
    sy = lambda v: CH - CB - v / ymax * (CH - CT - CB)
    for i in range(5):
        v = ymax * i / 4
        y = sy(v)
        p.append(f'<line x1="{CL}" y1="{y:.1f}" x2="{CW - CR}" y2="{y:.1f}" stroke="{t["grid"]}"/>')
        p.append(f'<text x="{CL - 8}" y="{y + 4:.1f}" font-size="12" fill="{t["muted"]}" '
                 f'text-anchor="end">{esc(yfmt(v))}</text>')
    for d in FLOOR_DEPTHS:
        p.append(f'<text x="{sx(d):.1f}" y="{CH - CB + 18}" font-size="12" fill="{t["muted"]}" '
                 f'text-anchor="middle">{d}</text>')
    p.append(f'<text x="{CW - CR}" y="{CH - 10}" font-size="12" fill="{t["muted"]}" '
             f'text-anchor="end">depth of the world (log scale)</text>')
    p.append(f'<line x1="{CL}" y1="{CH - CB}" x2="{CW - CR}" y2="{CH - CB}" stroke="{t["axis"]}"/>')
    for _, slot, dashed, pts in series:
        pts = [(d, v) for d, v in pts if not math.isnan(v)]
        if not pts:
            continue
        c = t["series"][slot]
        d = ' stroke-dasharray="5 4"' if dashed else ""
        path_d = " ".join(f"{'M' if i == 0 else 'L'}{sx(a):.1f},{sy(b):.1f}"
                          for i, (a, b) in enumerate(pts))
        p.append(f'<path d="{path_d}" fill="none" stroke="{c}" stroke-width="2" '
                 f'stroke-linejoin="round"{d}/>')
        for a, b in pts:
            p.append(f'<circle cx="{sx(a):.1f}" cy="{sy(b):.1f}" r="3" fill="{c}"/>')
    p.append("</svg>")
    path.write_text("\n".join(p) + "\n")


def report():
    OUT.mkdir(parents=True, exist_ok=True)
    groups = {}
    for series, w, h, d, p in sweep_configs():
        runs = [summarise(load(run_path(w, h, d, p, s)), SWEEP_UNITS, SWEEP_EVERY)
                for s in SWEEP_SEEDS if run_path(w, h, d, p, s).exists()]
        if runs:
            groups[(series, w, h, d, p)] = runs

    md = []

    def table(series, pitch):
        md.append("| world | alive at the end | hunting lasts | organisms | green | hunters "
                  "| bites / unit | flat codes | lying level |")
        md.append("|---|---|---|---|---|---|---|---|---|")
        for (s, w, h, d, p), runs in groups.items():
            if s != series or p != pitch:
                continue
            live = [r for r in runs if r["alive"]]
            md.append(
                f"| {w} x {h} x {d} | {sum(r['alive'] for r in runs)} of {len(runs)} "
                f"| {sum(r['hunting'] for r in runs)} of {len(runs)} "
                f"| {fmt(med([r['pop'] for r in live]), 0)} "
                f"| {pct(med([r['green'] for r in live]))} "
                f"| {pct(med([r['hunters'] for r in live]))} "
                f"| {fmt(med([r['bites'] for r in live]), 1)} "
                f"| {pct(med([r['flat'] for r in live]))} "
                f"| {fmt(med([r['level'] for r in live]), 2)} |")
        md.append("")

    tables = {}
    for series in ("floor", "volume"):
        for pitch in PITCHES:
            md.clear()
            table(series, pitch)
            tables[(series, pitch)] = "\n".join(md)

    def pts(series, pitch, key):
        return [(d, med([r[key] for r in runs if r["alive"]]))
                for (s, w, h, d, p), runs in groups.items() if s == series and p == pitch]

    def share_of_worlds(series, pitch, key):
        return [(d, sum(r[key] for r in runs) / len(runs))
                for (s, w, h, d, p), runs in groups.items() if s == series and p == pitch]

    hunt_series = [("floor, flat genes", 0, False, share_of_worlds("floor", 0, "hunting")),
                   ("floor, bent genes", 1, False, share_of_worlds("floor", 50, "hunting")),
                   ("same volume, flat", 0, True, share_of_worlds("volume", 0, "hunting")),
                   ("same volume, bent", 1, True, share_of_worlds("volume", 50, "hunting"))]
    flat_series = [("floor", 2, False, pts("floor", 50, "flat")),
                   ("same volume", 2, True, pts("volume", 50, "flat"))]
    for theme in THEMES:
        chart(theme, "Worlds where hunting lasts",
              "Share of 24 worlds with at least one bite, kill or infection per time unit "
              "in time 300-400", hunt_series, 1.0, lambda v: f"{100 * v:.0f}%",
              OUT / f"hunting-{theme}.svg")
        chart(theme, "Bodies that stayed flat, when half of new genes bend",
              "Share of living organisms whose every gene has pitch 0, at time 400; median of 24 worlds",
              flat_series, 1.0, lambda v: f"{100 * v:.0f}%", OUT / f"flat-{theme}.svg")

    # beings
    rows = []
    for i, being in enumerate(beings()):
        cells = []
        for shape in SHAPES:
            for mode in ("alone", "among"):
                runs = [summarise(load(f), BEING_UNITS, BEING_EVERY)
                        for f in sorted((RUNS / "beings").glob(f"b{i:02d}-{shape}-{mode}-s*.csv"))]
                if not runs:
                    cells.append("-")
                    continue
                kept = sum(r["marked"] > 0 for r in runs)
                share = med([r["marked_share"] for r in runs])
                cells.append(f"{kept}/{len(runs)}, {pct(share)}" if mode == "among"
                             else f"{kept}/{len(runs)}, {fmt(med([r['marked'] for r in runs]), 0)}")
        rows.append(f"| {esc(being_tag(being))} | " + " | ".join(cells) + " |")
    (OUT / "tables.md").write_text(
        "\n\n".join(f"<!-- {s} pitch {p} -->\n{t}" for (s, p), t in tables.items())
        + "\n\n<!-- beings -->\n"
        + "| being | slab, alone | slab, among | default, alone | default, among "
          "| cube, alone | cube, among |\n|---|---|---|---|---|---|---|\n"
        + "\n".join(rows) + "\n")
    print(f"wrote {OUT / 'tables.md'} and the charts")


if __name__ == "__main__":
    if len(sys.argv) != 2 or sys.argv[1] not in ("run", "report"):
        print(__doc__)
        sys.exit(2)
    run() if sys.argv[1] == "run" else report()
