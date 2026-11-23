#!/usr/bin/env python3
"""Build every compiler x optimisation cell, run each binary with several seeds on a
pinned core, and write results/matrix.csv and results/matrix.md.

A target is flagged when max |t| exceeds both the usual dudect threshold (4.5) and a
Bonferroni-corrected threshold over all targets and tests (family-wise alpha, default
0.001). Per cell, results are aggregated over seeds: '*' = flagged in every seed,
'~' = flagged in some seeds only."""
import argparse, csv, itertools, math, os, subprocess, sys, collections

COMPILERS = ["gcc", "clang"]
OPTS = ["-O0", "-O1", "-O2", "-O3", "-Os"]
BINS = ["ct_targets", "ct_mlkem", "ct_extra"]
NTESTS = 7  # raw + 5 crops + second order


def z_threshold(alpha):
    """Two-sided normal quantile, by bisection."""
    lo, hi = 0.0, 40.0
    for _ in range(100):
        mid = (lo + hi) / 2
        if math.erfc(mid / math.sqrt(2)) > alpha:
            lo = mid
        else:
            hi = mid
    return hi


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", default="3")
    ap.add_argument("--n", default="300000")
    ap.add_argument("--seeds", nargs="+", default=["1", "2", "3"])
    ap.add_argument("--compilers", nargs="+", default=COMPILERS)
    ap.add_argument("--opts", nargs="+", default=OPTS)
    ap.add_argument("--alpha", type=float, default=0.001)
    a = ap.parse_args()
    os.makedirs("results", exist_ok=True)
    rows = []  # cell, target, seed, max|t|, second-order t
    for cc, opt in itertools.product(a.compilers, a.opts):
        r = subprocess.run(["make", "-s", "build", f"CC={cc}", f"OPT={opt}"], capture_output=True, text=True)
        if r.returncode:
            print(f"build failed for {cc} {opt}:\n{r.stderr}", file=sys.stderr)
            continue
        tag = f"{cc}{opt.replace('-', '_')}"
        for seed in a.seeds:
            for b in BINS:
                out = subprocess.run(["taskset", "-c", a.core, f"build/{tag}/{b}", a.n, "-", seed],
                                     capture_output=True, text=True, check=True).stdout
                for line in out.splitlines():
                    if line.startswith("RESULT "):
                        p = line.split()
                        rows.append((f"{cc} {opt}", p[1], int(seed), float(p[-1]), float(p[-2])))
                        print(f"{cc} {opt} seed={seed} {p[1]:<24} max|t|={p[-1]}", flush=True)
    with open("results/matrix.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["cell", "target", "seed", "max_abs_t", "t_second_order"])
        w.writerows(rows)
    cells = sorted({r[0] for r in rows}, key=lambda c: (c.split()[0], OPTS.index(c.split()[1])))
    targets = list(dict.fromkeys(r[1] for r in rows))
    m = len(targets) * NTESTS
    thr = max(4.5, z_threshold(a.alpha / m))
    g = collections.defaultdict(list)
    for c, t, s, v, _ in rows:
        g[(c, t)].append(v)
    with open("results/matrix.md", "w") as f:
        f.write(f"Threshold: |t| > {thr:.2f} (max of 4.5 and Bonferroni over {m} tests, alpha={a.alpha}). "
                f"Cells show min-max of max|t| over seeds {', '.join(a.seeds)}; "
                "`*` = flagged in every seed, `~` = flagged in some seeds.\n\n")
        f.write("| target | " + " | ".join(cells) + " |\n|---|" + "---|" * len(cells) + "\n")
        for t in targets:
            cs = []
            for c in cells:
                v = g.get((c, t))
                if not v:
                    cs.append("n/a")
                    continue
                flag = sum(x > thr for x in v)
                mark = "*" if flag == len(v) else "~" if flag else ""
                cs.append(f"{min(v):.1f}-{max(v):.1f}{mark}")
            f.write(f"| {t} | " + " | ".join(cs) + " |\n")
    print(open("results/matrix.md").read())


if __name__ == "__main__":
    main()
