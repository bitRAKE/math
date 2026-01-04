#!/usr/bin/env python3
"""
plot_kn_bounds.py

Plots k(n) directly (original Erdős #962 formulation) by inverting the m(k) data:

Given plateau points (k, m(k)) from km_plateaus.csv,
we plot the "first attainment" points (n=m(k), k) and optionally the step function
k(n) = max{k : m(k) <= n}.

Also overlays the standard bounds in the same direction:
  - Erdős lower:  exp((log n)^(1/2 - eps))  (shape; constant depends on eps)
  - Tang lower:   exp((1/sqrt(2))*sqrt(log n * log log n))  (shape; drop o(1))
  - Tao upper:    sqrt(n)  (shape; drop (1+o(1)))

Usage:
  python plot_kn_bounds.py
  python plot_kn_bounds.py --nmin 1e6 --nmax 1e14 --out kn_focus.png --step
  python plot_kn_bounds.py --erdos-eps 0.1 --erdos-scale 10
"""

import argparse
import csv
import math
from bisect import bisect_right
from pathlib import Path

import matplotlib.pyplot as plt


def read_plateaus(csv_path: Path):
    """
    Accepts either:
      - headered CSV with columns k,m
      - or two-column CSV without header
    Returns sorted list of (k, m).
    """
    rows = []
    with open(csv_path, newline="") as f:
        sample = f.read(4096)
        f.seek(0)
        has_header = ("k" in sample.lower() and "m" in sample.lower())

        if has_header:
            rdr = csv.DictReader(f)
            for r in rdr:
                if not r:
                    continue
                k = int(str(r["k"]).strip())
                m = int(str(r["m"]).strip())
                rows.append((k, m))
        else:
            rdr = csv.reader(f)
            for r in rdr:
                if not r:
                    continue
                k = int(str(r[0]).strip())
                m = int(str(r[1]).strip())
                rows.append((k, m))

    rows.sort()
    return rows


def log_grid(xmin: float, xmax: float, points: int):
    xmin = float(xmin)
    xmax = float(xmax)
    if xmin <= 0 or xmax <= 0 or xmax <= xmin:
        return [xmin]

    lo = math.log(xmin)
    hi = math.log(xmax)
    out = []
    last = None
    for i in range(points + 1):
        x = math.exp(lo + (hi - lo) * (i / points))
        # keep floats; grid is for smooth curves
        if last is None or x != last:
            out.append(x)
            last = x
    return out


# ---- Bound "shapes" (drop constants / o(1) terms) ----

def tao_upper(n: float) -> float:
    # k(n) <= (1+o(1))*sqrt(n)  -> plot sqrt(n)
    return math.sqrt(n)


def tang_lower(n: float) -> float:
    # k(n) >= exp((1/sqrt(2)-o(1))*sqrt(log n log log n))
    # -> plot exp((1/sqrt(2))*sqrt(log n log log n)) for n>e^e
    if n <= math.e ** math.e:
        return float("nan")
    L = math.log(n)
    return math.exp((1.0 / math.sqrt(2.0)) * math.sqrt(L * math.log(L)))


def erdos_lower(n: float, eps: float, scale: float) -> float:
    # Erdős: k(n) >>_eps exp((log n)^(1/2 - eps))
    # -> plot scale * exp((log n)^(1/2 - eps))
    if n <= 1.0:
        return float("nan")
    L = math.log(n)
    power = 0.5 - eps
    if power <= 0:
        return float("nan")
    return scale * math.exp(L ** power)


# ---- k(n) from m(k) plateau data ----

def k_of_n_from_mk(n: float, mk_sorted_by_m):
    """
    mk_sorted_by_m: list of (m, k) sorted by m increasing
    returns max k such that m(k) <= n (step function)
    """
    ms = [mk[0] for mk in mk_sorted_by_m]
    idx = bisect_right(ms, n) - 1
    if idx < 0:
        return 0
    return mk_sorted_by_m[idx][1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="km_plateaus.csv", help="Input CSV with plateau points k,m")
    ap.add_argument("--out", default="kn_bounds.png", help="Output PNG path")
    ap.add_argument("--nmin", type=float, default=None, help="Min n to plot (default: min m in data)")
    ap.add_argument("--nmax", type=float, default=None, help="Max n to plot (default: max m in data)")
    ap.add_argument("--grid", type=int, default=700, help="Log-grid size for smooth curves/step sampling")

    ap.add_argument("--no-tao", action="store_true", help="Disable Tao upper curve")
    ap.add_argument("--no-tang", action="store_true", help="Disable Tang lower curve")
    ap.add_argument("--no-erdos", action="store_true", help="Disable Erdős lower curve")

    ap.add_argument("--erdos-eps", type=float, default=0.1, help="Epsilon for Erdős lower bound shape")
    ap.add_argument("--erdos-scale", type=float, default=1.0, help="Vertical scale for Erdős curve (implicit constant)")
    ap.add_argument("--step", action="store_true", help="Also plot step-function k(n)=max{k:m(k)<=n}")
    ap.add_argument("--title", default="Erdős #962: k(n) from inverted m(k) data with bounds (direct orientation)")
    args = ap.parse_args()

    pts_km = read_plateaus(Path(args.csv))
    if not pts_km:
        raise SystemExit("No data loaded.")

    # Invert data as (n=m, k) "first attainment" points
    n_data = [m for (k, m) in pts_km]
    k_data = [k for (k, m) in pts_km]

    nmin = args.nmin if args.nmin is not None else float(min(n_data))
    nmax = args.nmax if args.nmax is not None else float(max(n_data))

    # For step function: sort by m
    mk_sorted_by_m = sorted([(m, k) for (k, m) in pts_km], key=lambda x: x[0])

    # Smooth grid for bounds (float)
    grid = log_grid(nmin, nmax, args.grid)

    plt.figure(figsize=(9, 6))

    # data points
    plt.loglog(n_data, k_data, marker="o", linestyle="None", label="Data (inverted): points (n=m(k), k)")

    # step function sampling
    if args.step:
        k_step = [k_of_n_from_mk(x, mk_sorted_by_m) for x in grid]
        plt.loglog(grid, k_step, label="Step function: k(n)=max{k: m(k)≤n}")

    if not args.no_tao:
        plt.loglog(grid, [tao_upper(x) for x in grid], label="Tao upper (shape): k(n) ≈ √n")

    if not args.no_tang:
        plt.loglog(grid, [tang_lower(x) for x in grid], label="Tang lower (shape): exp((1/√2)√(log n log log n))")

    if not args.no_erdos:
        plt.loglog(
            grid,
            [erdos_lower(x, args.erdos_eps, args.erdos_scale) for x in grid],
            label=f"Erdős lower (shape): scale·exp((log n)^(1/2-ε)), ε={args.erdos_eps:g}",
        )

    plt.xlabel("n (upper bound on start m)")
    plt.ylabel("k(n)")
    plt.title(args.title)
    plt.grid(True, which="both", linewidth=0.5)
    plt.legend()
    plt.tight_layout()

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(out_path, dpi=180)
    plt.close()

    print(f"Wrote: {out_path}")


if __name__ == "__main__":
    main()
