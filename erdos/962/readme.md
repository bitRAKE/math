# Erdős Problem #962 — consecutive integers with a “large” prime factor

This directory records a computational exploration of Erdős Problem #962 (ErdosProblems forum thread 962), using a plateau-compressed dataset and a plotting script that keeps the **original direction of the problem**: we plot **k(n)** against **n**, rather than inverting the theoretical bounds.

Source threads/paper (kept as raw URLs inside code blocks per repo conventions):
```text
https://www.erdosproblems.com/forum/thread/962
https://github.com/QuanyuTang/erdos-problem-962/blob/main/On_Erd%C5%91s_Problem_962.pdf
````

---

## Problem statement and the two equivalent viewpoints

For an integer `k > 0`, define `m(k)` as the smallest integer `m` such that the block

`m+1, m+2, …, m+k`

has the property that **each** term is divisible by at least one prime `> k`.

Equivalently: none of `m+1..m+k` is **k-smooth** (a number is k-smooth if all its prime factors are `≤ k`).

### The original ErdősProblems function: `k(n)`

In the ErdősProblems formulation, one typically fixes an upper limit `n` on the start point and asks for the **largest** `k` achievable:

> `k(n)` = the maximum `k` such that there exists some `m ≤ n` for which `m+1..m+k` all have a prime factor `> k`.

### Exact inversion: data → `k(n)` without changing the bounds

If we know `m(k)` exactly (or at least at the `k` values we computed), then we get an **exact** relationship:

**k(n) = max { k : m(k) ≤ n }.**

This is the key perspective shift used here:

* We **invert the computed data** (which is exact).
* We **do not invert the theoretical bounds** (so Tao/Tang/Erdős results are shown in their native direction).

---

## Files

* `km_plateaus.csv`
  Plateau points for `m(k)` as `(k, m)` pairs.

* `km_bounds.png` (legacy orientation)
  A log–log plot of `m` vs `k` (useful, but requires inverting bounds to compare to `k(n)` results).

* `kn_bounds.png` (preferred orientation, if present)
  A log–log plot of `k(n)` vs `n` constructed by **inverting the data**, with Erdős/Tao/Tang bounds drawn directly as statements about `k(n)`.

If you only have the CSV + scripts, the PNGs are fully regenerable.

---

## Data format: plateau points

The CSV stores **plateau points** of `m(k)`:

* When `m(k)` stays constant over a range of k (a plateau), only the first `k` of that plateau is stored.
* Missing `k` values inherit the same `m` as the most recent listed plateau point.

This is compact and preserves all information needed to reconstruct:

1. the full step function `m(k)` for `k` in a range, and
2. the exact “attainment points” `(n=m(k), k)` used to build `k(n)`.

---

## How `k(n)` is built from the CSV

Let the plateau points be `(k_i, m_i)` sorted by increasing `k`.

Interpretation:

* `m(k) = m_i` for `k_i ≤ k < k_{i+1}` (until the next plateau point).

To compute `k(n)` from this:

1. Reinterpret each plateau point as an attainment point `(n_i = m_i, k_i)`.
2. Sort by `n_i` (start position).
3. Then

   * `k(n) = max { k_i : n_i ≤ n }`.

This is the step function plotted in the preferred figure.

---

## Bounds plotted in the preferred orientation

The plot overlays computed `k(n)` (from inverted data) with standard asymptotic bounds stated directly for `k(n)`:

* **Upper bound (Tao):** `k(n) ≤ (1+o(1)) n^{1/2}`
  Plotted as the shape curve `k = sqrt(n)`.

* **Lower bound (Tang):**
  `k(n) ≥ exp((1/sqrt(2) − o(1)) * sqrt(log n * log log n))`
  Plotted by dropping the `o(1)` term.

* **Lower bound (Erdős, ε-family):** for any `ε > 0`,
  `k(n) ≫_ε exp((log n)^{1/2 − ε})`
  For visualization we choose a specific `ε` (and an arbitrary scale constant, because `≫_ε` hides constants).

Notes:

* These curves are used as *growth guides*; they are asymptotic and involve suppressed constants and/or `o(1)` terms.
* The critical point is that they are shown in the **same direction they are stated**, avoiding interpretive friction.

---

## Reproducing the plots

### Preferred plot: `k(n)` vs `n` (invert the data, not the bounds)

The script expects `km_plateaus.csv` and generates a `k(n)` plot.

Example usage:

```bash
python -m pip install -r requirements.txt
python plot_kn_bounds.py --csv km_plateaus.csv --out erdos_962_kn_bounds.png --step
```

Focus on a window (useful when extending computations):

```bash
python plot_kn_bounds.py --nmin 1e9 --nmax 1e14 --out kn_1e9_1e14.png --step
```

Adjust the Erdős lower curve parameterization:

```bash
python plot_kn_bounds.py --erdos-eps 0.1 --erdos-scale 5 --out kn_erdos_eps01.png --step
```

---

## Computational check used to validate a block

A number `x` has a prime factor `> k` iff it is **not** k-smooth.

Efficient test for fixed `k`:

1. Precompute primes `≤ k`.
2. Divide `x` by those primes until no longer divisible.
3. If the remaining value is `> 1`, then `x` had a prime factor `> k`.

A candidate start `m` is valid for length `k` iff the test succeeds for all `x = m+1..m+k`.

---

## “Next time I open this repo…”

Recommended workflow when adding new computed points:

1. Append new plateau points to `km_plateaus.csv`.
2. Regenerate the preferred plot:

```bash
python plot_kn_bounds.py --csv km_plateaus.csv --out erdos_962_kn_bounds.png --step
```

3. Optionally regenerate a focused window plot around new data to see local behavior:

```bash
python plot_kn_bounds.py --nmin <...> --nmax <...> --out kn_focus.png --step
```

---

## Ideas for further exploration

* Store both representations:

  * plateau points `(k, m(k))`,
  * and a dense sampled step function `(n, k(n))` over ranges of `n`.
* Examine normalized growth:

  * `k(n)/sqrt(n)` vs `n`,
  * `log k(n) / sqrt(log n log log n)` vs `n`,
  * sensitivity to Erdős’ `ε` and the hidden constant.
* Characterize jump structure:

  * where plateaus end,
  * correlation with primorial-like structure and dense smoothness regions.
