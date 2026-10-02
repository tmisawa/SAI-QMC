#!/usr/bin/env python3
"""Grand-canonical finite-temperature ED of a small Hubbard cluster on an Lx x Ly grid.

H = sum_{ij,s} K_ij c+_is c_js + U sum_i n_iu n_id, averaged with exp[-beta (H - mu N)],
mu = U/2. K is read from a DQMC `hopping_used.txt`, so boundary signs and the doubled bond
of a length-2 periodic direction are identical to the DQMC run. Site i sits at
(x, y) = (i % Lx, i // Lx). Every (N_up, N_down) sector is diagonalized in full.
Energies are in units of |t| = 1.

For each beta the JSON output has
  E_hub_per_site   <H>/N (without -mu N), the convention of DQMC's E_hub/N
  doublon_per_site <n_up n_down> per site
  ntot             <N>
  Szz              N^-1 sum_ij cos[q.(r_i - r_j)] <Sz_i Sz_j>, Sz = (n_up - n_down)/2,
                   for every q = 2 pi (mx/Lx, my/Ly)
H is SU(2) invariant, so Sperp(q) = 2 Szz(q) exactly.

Requires Python 3 and numpy.
usage: python3 ed_finite_t.py HOPPING Lx Ly U BETA[,BETA...] > reference.json
"""
import itertools
import json
import sys

import numpy as np


def species(K, n):
    """One-species hopping matrix in the n-particle occupation basis."""
    N = len(K)
    states = [sum(1 << i for i in c) for c in itertools.combinations(range(N), n)]
    index = {s: a for a, s in enumerate(states)}
    T = np.zeros((len(states), len(states)))
    for a, s in enumerate(states):
        for j in range(N):
            if not s >> j & 1:
                continue
            for i in range(N):
                if i == j or K[i, j] == 0.0 or s >> i & 1:
                    continue
                removed = s ^ (1 << j)
                sign = (-1) ** (bin(s & ((1 << j) - 1)).count('1') +
                                bin(removed & ((1 << i) - 1)).count('1'))
                T[index[removed | 1 << i], a] += sign * K[i, j]
    return T, states


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    path, Lx, Ly, U = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4])
    betas = [float(b) for b in sys.argv[5].split(',')]
    with open(path) as f:
        words = f.read().split()
    N = int(words[0])
    K = np.array(words[1:], dtype=float).reshape(N, N)
    if N != Lx * Ly or not np.array_equal(K, K.T) or np.any(np.diag(K) != 0.0):
        sys.exit('hopping matrix does not match a real symmetric zero-diagonal Lx*Ly grid')
    x = np.arange(N) % Lx
    y = np.arange(N) // Lx
    qs = [(mx, my) for my in range(Ly) for mx in range(Lx)]
    phase = np.array([np.exp(2j * np.pi * (mx * x / Lx + my * y / Ly)) for mx, my in qs])
    cache = {n: species(K, n) for n in range(N + 1)}
    bits = {n: np.array([[s >> i & 1 for i in range(N)] for s in cache[n][1]], dtype=float)
            for n in range(N + 1)}
    sectors = []
    for nu in range(N + 1):
        Tu = cache[nu][0]
        for nd in range(N + 1):
            Td = cache[nd][0]
            bu = bits[nu][:, None, :]
            bd = bits[nd][None, :, :]
            double = (bu * bd).sum(-1).reshape(-1)
            sz = (0.5 * (bu - bd)).reshape(-1, N)
            szq = np.abs(sz @ phase.T) ** 2 / N  # basis state x q
            H = (np.kron(Tu, np.eye(len(Td))) + np.kron(np.eye(len(Tu)), Td) +
                 U * np.diag(double))
            w, v = np.linalg.eigh(H)
            p = v ** 2
            sectors.append(dict(n=nu + nd, e=w, d=double @ p, szq=szq.T @ p))
    shift = min((s['e'] - 0.5 * U * s['n']).min() for s in sectors)
    results = []
    for beta in betas:
        Z = E = D = Nt = 0.0
        S = np.zeros(len(qs))
        for s in sectors:
            weight = np.exp(-beta * (s['e'] - 0.5 * U * s['n'] - shift))
            Z += weight.sum()
            E += weight @ s['e']
            D += weight @ s['d']
            Nt += weight.sum() * s['n']
            S += s['szq'] @ weight
        results.append(dict(
            beta=beta, E_hub_per_site=E / Z / N, doublon_per_site=D / Z / N, ntot=Nt / Z,
            Szz=[dict(mx=mx, my=my, value=float(v)) for (mx, my), v in zip(qs, S / Z)]))
    json.dump(dict(sites=N, Lx=Lx, Ly=Ly, U=U, mu=U / 2, hopping=path, results=results),
              sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    main()
