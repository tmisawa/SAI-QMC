"""Independent hopping matrix of a chain/square lattice and a check of a DQMC hopping_used.txt.

Elements are sums over images of the displacements (+-1,0), (0,+-1) with t and
(+-1,+-1) with tprime: an unwrapped target beyond an open edge is dropped and
an antiperiodic direction contributes (-1)^winding. This does not use the
solver's add_bond construction.

usage: bond_table.py Lx Ly t tprime bc_x bc_y OUT [--check hopping_used.txt]
       bc = periodic | antiperiodic | open; a chain is Ly=1 with bc_y=open.
"""
import argparse
import sys


def matrix(Lx, Ly, t, tp, bx, by):
    n = Lx * Ly
    K = [[0.0] * n for _ in range(n)]
    disp = [(1, 0, t), (-1, 0, t), (0, 1, t), (0, -1, t)]
    if tp != 0.0:
        disp += [(1, 1, tp), (1, -1, tp), (-1, 1, tp), (-1, -1, tp)]
    for i in range(n):
        xi, yi = i % Lx, i // Lx
        for dx, dy, amp in disp:
            ux, uy = xi + dx, yi + dy
            wx, wy = ux // Lx, uy // Ly
            if (wx and (bx == 'open' or Lx == 1)) or (wy and (by == 'open' or Ly == 1)):
                continue
            if (dy and Ly == 1) or (dx and Lx == 1):
                continue
            j = (ux - wx * Lx) + Lx * (uy - wy * Ly)
            if bx == 'antiperiodic' and wx % 2:
                amp = -amp
            if by == 'antiperiodic' and wy % 2:
                amp = -amp
            K[i][j] += amp
    return K


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('Lx', type=int)
    ap.add_argument('Ly', type=int)
    ap.add_argument('t', type=float)
    ap.add_argument('tprime', type=float)
    ap.add_argument('bc_x')
    ap.add_argument('bc_y')
    ap.add_argument('out')
    ap.add_argument('--check')
    a = ap.parse_args()
    K = matrix(a.Lx, a.Ly, a.t, a.tprime, a.bc_x, a.bc_y)
    n = len(K)
    with open(a.out, 'w') as f:
        f.write(f'{n}\n')
        for row in K:
            f.write(' '.join(repr(v) for v in row) + '\n')
    if a.check:
        words = open(a.check).read().split()
        if int(words[0]) != n:
            sys.exit(f'size mismatch {words[0]} vs {n}')
        got = [float(w) for w in words[1:]]
        bad = [(i, j, got[i * n + j], K[i][j]) for i in range(n) for j in range(n)
               if got[i * n + j] != K[i][j]]
        if bad:
            sys.exit(f'{a.check}: {len(bad)} elements differ, first {bad[0]}')
        print(f'{a.check}: identical to the independent bond table ({n}x{n})')


if __name__ == '__main__':
    main()
