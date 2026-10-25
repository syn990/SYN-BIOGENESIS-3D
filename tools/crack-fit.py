#!/usr/bin/env python3
# SYN-BIOGENESIS-3D: the telescope's fitness (src/syn/crack.c, docs/crack),
# fitted on tools/founders' CSV files: a conditional logit, one choice per
# world among its founders, of which founder's bloodline leads at the end.
# Five folds by world for the held-out figures, then the fit on all.
# Usage: tools/crack-fit.py DIR (DIR/part*.csv)
import glob, math, random, sys
rows = {}
for f in sorted(glob.glob(sys.argv[1] + "/part*.csv")):
    for line in open(f):
        v = line.strip().split(",")
        if len(v) < 34: continue
        seed = int(v[0]); i = int(v[1]); lead = int(v[2]); alive_i = int(v[5]); pop = int(v[6])
        x = [float(t) for t in v[9:34]]
        rows.setdefault(seed, []).append((i, lead, alive_i, pop, x))
NAMES = ["ngenes","sym","mirror","bent","green","red","cyan","blue","white","gray","yellow","mass","repro","z","light",
         "d_light","d_carbon","d_plants","d_hunters","d_dead","d_crowd","purple","height","plan","cone"]
def feats(x, place):
    g = dict(zip(NAMES, x))
    f = [math.log1p(g["green"]), math.log1p(g["red"]), math.log1p(g["cyan"]), math.log1p(g["blue"]),
         math.log1p(g["white"]), math.log1p(g["gray"]), math.log1p(g["yellow"]), math.log(g["repro"]),
         g["sym"] / 8, g["bent"] / max(1, g["ngenes"]),
         g["d_light"], g["d_carbon"], g["d_plants"], g["d_hunters"], g["d_dead"], g["d_crowd"],
         g["d_light"] * math.log1p(g["green"]) / 5,
         math.log1p(g["purple"]), g["height"], 1.0 if g["plan"] == 1 else 0.0, 1.0 if g["plan"] == 2 else 0.0]
    if place:
        f += [g["light"], g["light"] * math.log1p(g["green"]) / 5]
    return f
FN = ["lgreen","lred","lcyan","lblue","lwhite","lgray","lyellow","lrepro","sym","bentfrac",
      "dlight","dcarbon","dplants","dhunters","ddead","dcrowd","dlight*lgreen","lpurple","height","cone",
      "orb"]
def data(place):
    D = []
    for s, r in rows.items():
        if len(r) < 2: continue
        lead = r[0][1]
        if lead < 0: continue
        X = [feats(x, place) for (_, _, _, _, x) in r]
        y = [i for i, (fi, *_) in enumerate(r) if fi == lead]
        if not y: continue
        D.append((X, y[0]))
    return D
def fit(D, k, lam=0.05, iters=400, lr=0.3):
    b = [0.0] * k
    for it in range(iters):
        g = [0.0] * k
        for X, y in D:
            s = [sum(bi * xi for bi, xi in zip(b, x)) for x in X]
            m = max(s); e = [math.exp(t - m) for t in s]; Z = sum(e)
            for j in range(k):
                g[j] += X[y][j] - sum(e[i] * X[i][j] for i in range(len(X))) / Z
        for j in range(k):
            b[j] += lr * (g[j] / len(D) - lam * b[j])
    return b
def score(D, b):
    ll = 0; top = 0; base = 0
    for X, y in D:
        s = [sum(bi * xi for bi, xi in zip(b, x)) for x in X]
        m = max(s); Z = sum(math.exp(t - m) for t in s)
        ll += s[y] - m - math.log(Z); base += -math.log(len(X))
        top += s[y] == max(s)
    return ll / len(D), base / len(D), top / len(D)
for place in (False, True):
    D = data(place); k = len(D[0][0][0])
    random.seed(1); random.shuffle(D)
    folds = 5; res = []
    for f in range(folds):
        test = D[f::folds]; train = [d for i, d in enumerate(D) if i % folds != f]
        b = fit(train, k)
        res.append(score(test, b))
    ll = sum(r[0] for r in res) / folds; base = sum(r[1] for r in res) / folds; top = sum(r[2] for r in res) / folds
    print(f"place={place} seeds={len(D)} held-out: loglik/seed {ll:.3f} vs uniform {base:.3f}; top-1 {top:.2%} (uniform {1/30:.1%})")
    b = fit(D, k)
    names = FN + (["light","light*lgreen"] if place else [])
    print("  " + "  ".join(f"{n}={v:+.2f}" for n, v in zip(names, b)))
