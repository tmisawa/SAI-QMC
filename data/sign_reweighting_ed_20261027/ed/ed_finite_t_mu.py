"""Grand-canonical finite-temperature ED with a general chemical potential.

New version of docs/design/directional-boundary-20261002-evidence/ed-prototype/ed_finite_t.py
(kept unchanged). Changes: mu is a CLI argument instead of U/2; the Boltzmann
weight is exp[-beta (H - mu N)] with the shift min(E - mu N) over all sectors;
the output records mu, the total and per-site E_hub, E_gc = E_hub - mu N,
ntot and the per-site doublon, and log Z; Szz(q) is optional.

H = sum_{ij,s} K_ij c+_is c_js + U sum_i n_iu n_id (SAI-QMC convention,
mu = U/2 is half filling on a bipartite lattice). K is read from a dense
matrix file (first entry n, then n*n elements). Site i sits at
(x, y) = (i % Lx, i // Lx). Every (N_up, N_down) sector is diagonalized.

usage: ed_finite_t_mu.py HOPPING Lx Ly U MU BETA[,BETA...] [--szz] > reference.json
"""
import argparse
import itertools
import json
import math
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
    ap = argparse.ArgumentParser()
    ap.add_argument('hopping')
    ap.add_argument('Lx', type=int)
    ap.add_argument('Ly', type=int)
    ap.add_argument('U', type=float)
    ap.add_argument('mu', type=float)
    ap.add_argument('betas')
    ap.add_argument('--szz', action='store_true')
    a = ap.parse_args()
    betas = [float(b) for b in a.betas.split(',')]
    words = open(a.hopping).read().split()
    N = int(words[0])
    K = np.array(words[1:], dtype=float).reshape(N, N)
    if N != a.Lx * a.Ly or not np.array_equal(K, K.T) or np.any(np.diag(K) != 0.0):
        sys.exit('hopping matrix does not match an Lx*Ly real symmetric zero-diagonal grid')
    x = np.arange(N) % a.Lx
    y = np.arange(N) // a.Lx
    qs = [(mx, my) for my in range(a.Ly) for mx in range(a.Lx)]
    phase = np.array([np.exp(2j * np.pi * (mx * x / a.Lx + my * y / a.Ly)) for mx, my in qs])
    cache = {n: species(K, n) for n in range(N + 1)}
    bits = {n: np.array([[s >> i & 1 for i in range(N)] for s in cache[n][1]], dtype=float)
            .reshape(-1, N) for n in range(N + 1)}
    sectors = []
    for nu in range(N + 1):
        Tu = cache[nu][0]
        for nd in range(N + 1):
            Td = cache[nd][0]
            bu = bits[nu][:, None, :]
            bd = bits[nd][None, :, :]
            double = (bu * bd).sum(-1).reshape(-1)
            H = (np.kron(Tu, np.eye(len(Td))) + np.kron(np.eye(len(Tu)), Td) +
                 a.U * np.diag(double))
            w, v = np.linalg.eigh(H)
            p = v ** 2
            sec = dict(n=nu + nd, e=w, d=double @ p)
            if a.szz:
                sz = (0.5 * (bu - bd)).reshape(-1, N)
                sec['szq'] = (np.abs(sz @ phase.T) ** 2 / N).T @ p
            sectors.append(sec)
    shift = min((s['e'] - a.mu * s['n']).min() for s in sectors)
    results = []
    for beta in betas:
        Z = E = D = Nt = 0.0
        S = np.zeros(len(qs))
        for s in sectors:
            weight = np.exp(-beta * (s['e'] - a.mu * s['n'] - shift))
            Z += weight.sum()
            E += weight @ s['e']
            D += weight @ s['d']
            Nt += weight.sum() * s['n']
            if a.szz:
                S += s['szq'] @ weight
        row = dict(beta=beta, E_hub=E / Z, E_hub_per_site=E / Z / N, ntot=Nt / Z,
                   E_gc=E / Z - a.mu * Nt / Z, doublon_per_site=D / Z / N,
                   logZ=math.log(Z) - beta * shift)
        if a.szz:
            row['Szz'] = [dict(mx=mx, my=my, value=float(v)) for (mx, my), v in zip(qs, S / Z)]
        results.append(row)
    json.dump(dict(sites=N, Lx=a.Lx, Ly=a.Ly, U=a.U, mu=a.mu, hopping=a.hopping,
                   results=results), sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    main()
