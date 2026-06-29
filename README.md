# math

Various mathematical explorations, implementations, and notes.  Most of the
repository is small, self-contained source or data for one problem family rather
than a single buildable application.

## Contents

| Path | Description |
| --- | --- |
| [`erdos/962/`](erdos/962/) | Computational notes, plateau data, verification helpers, and plots for Erdős Problem #962. |
| [`oeis/`](oeis/) | x86-64/fasmg sequence generators for selected OEIS entries. |
| [`Project Euler/`](Project%20Euler/) | Project Euler solutions in assembly, fasmg, text notes, VBScript, and Mathematica notebooks. |
| Root `*.g` files | Standalone fasmg/preprocessor experiments such as GCD, Pollard rho, Mersenne primes, and modular arithmetic. |
| [`SPN.txt`](SPN.txt) | Notes/data for an SPN-related exploration. |

## Tooling notes

There is no repository-wide build or test command.  Useful per-area entry points
are:

- `erdos/962/plot_kn_bounds.py` regenerates the `k(n)` plot from
  `erdos/962/km_plateaus.csv` when Python dependencies from
  `erdos/962/requirements.txt` are installed.
- `erdos/962/verify.py` and `erdos/962/verify_chain.py` provide bounded Python
  checks for the Erdős #962 plateau data.
- Assembly and fasmg examples are intentionally low-level experiments; compile
  individual files with the assembler/toolchain implied by their directory notes
  and file extension.

## License

This repository is released into the public domain under the terms in
[`LICENSE`](LICENSE).
