#!/usr/bin/env python3
"""Plot max |t| against the number of measurements for a leaking target, a clean
target and the null test, from results/trace_targets.csv."""
import csv, collections
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SHOW = [("memcmp_early_exit", "early-exit memcmp (leaks)"),
        ("div_q3329_secret", "secret-dependent division (leaks)"),
        ("memcmp_constant_time", "constant-time memcmp"),
        ("null_both_random", "null test (both classes random)")]

series = collections.defaultdict(lambda: ([], []))
with open("results/trace_targets.csv") as f:
    for row in csv.reader(f):
        name, n, ts = row[0], int(row[1]), [abs(float(x)) for x in row[2:]]
        series[name][0].append(n)
        series[name][1].append(max(ts))

fig, ax = plt.subplots(figsize=(7, 4.2))
for key, label in SHOW:
    if key in series:
        ax.plot(*series[key], label=label)
ax.axhline(4.5, color="k", ls="--", lw=1, label="threshold 4.5")
ax.set_xscale("log"); ax.set_yscale("log")
ax.set_xlabel("measurements"); ax.set_ylabel("max |t| over all tests")
ax.set_title("t-statistic versus number of measurements")
ax.legend(fontsize=8); ax.grid(alpha=0.3)
fig.tight_layout()
fig.savefig("results/t_vs_measurements.png", dpi=150)
print("wrote results/t_vs_measurements.png")
