#!/usr/bin/env python3
"""Generate the independent high-precision Green reference for F2.

The input is the raw UDV factor captured from the deterministic beta=33.325
trajectory. Values are interpreted as column-major C arrays. The calculation
does not use the AF_QMC inversion formulas.
"""

from pathlib import Path
import mpmath as mp


ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/udv_phase6a_actual_n16.txt"
mp.mp.dps = 500


def read_fixture(path):
    lines = path.read_text().splitlines()
    n = int(lines[0].split()[1])
    offset = mp.mpf(lines[1].split()[1])
    u0 = lines.index("U") + 1
    d0 = lines.index("D") + 1
    t0 = lines.index("T") + 1
    uflat = [mp.mpf(x) for x in lines[u0 : u0 + n * n]]
    d = [mp.mpf(x) for x in lines[d0 : d0 + n]]
    tflat = [mp.mpf(x) for x in lines[t0 : t0 + n * n]]
    U = mp.matrix(n, n)
    T = mp.matrix(n, n)
    for j in range(n):
        for i in range(n):
            U[i, j] = uflat[i + j * n]
            T[i, j] = tflat[i + j * n]
    return n, offset, U, d, T


n, offset, U, d, T = read_fixture(FIXTURE)
D = mp.diag([mp.exp(offset) * x for x in d])
A = mp.eye(n) + U * D * T
G = A ** -1
sign = 1 if mp.det(A) > 0 else -1

print(f"n {n}")
print(f"det_sign {sign}")
print("G")
for j in range(n):
    for i in range(n):
        print(mp.nstr(G[i, j], 17))
