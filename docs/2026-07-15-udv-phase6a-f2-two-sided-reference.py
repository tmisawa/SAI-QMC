#!/usr/bin/env python3
"""Generate a fixed non-diagonal two-sided UDV fixture and mp reference."""

import math
import mpmath as mp

mp.mp.dps = 500
n = 4


def eye():
    return [[1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]


def matmul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(n)) for j in range(n)]
            for i in range(n)]


def rotation(p, q, angle):
    a = eye()
    c, s = math.cos(angle), math.sin(angle)
    a[p][p], a[q][q] = c, c
    a[p][q], a[q][p] = -s, s
    return a


def upper(values):
    a = eye()
    k = 0
    for j in range(n):
        for i in range(j):
            a[i][j] = values[k]
            k += 1
    return a


def mp_matrix(a):
    return mp.matrix([[mp.mpf(repr(x)) for x in row] for row in a])


Ul = matmul(rotation(0, 1, 0.31), rotation(2, 3, -0.27))
Ur = matmul(rotation(0, 2, -0.19), rotation(1, 3, 0.23))
Tl = upper([0.12, -0.07, 0.09, 0.03, -0.11, 0.08])
Tr = upper([-0.05, 0.10, -0.04, 0.07, 0.06, -0.09])
loffset, roffset = 30.0, -20.0
lell = [300.0, 80.0, -250.0, -400.0]
rell = [250.0, -120.0, 50.0, -300.0]
lsign = [1.0, -1.0, 1.0, 1.0]
rsign = [-1.0, 1.0, 1.0, 1.0]
Dl = [lsign[i] * math.exp(lell[i] - loffset) for i in range(n)]
Dr = [rsign[i] * math.exp(rell[i] - roffset) for i in range(n)]

L = mp.e ** mp.mpf(loffset) * mp_matrix(Ul) * mp.diag(
    [mp.mpf(repr(x)) for x in Dl]) * mp_matrix(Tl)
R = mp.e ** mp.mpf(roffset) * mp_matrix(Ur) * mp.diag(
    [mp.mpf(repr(x)) for x in Dr]) * mp_matrix(Tr)
A = mp.eye(n) + L * R
G = A ** -1
sign = 1 if mp.det(A) > 0 else -1


def print_matrix(name, a):
    print(name)
    for j in range(n):
        for i in range(n):
            print(repr(a[i][j]))


print(f"n {n}")
print(f"left_offset {loffset}")
print_matrix("left_U", Ul)
print("left_D")
for x in Dl:
    print(repr(x))
print_matrix("left_T", Tl)
print(f"right_offset {roffset}")
print_matrix("right_U", Ur)
print("right_D")
for x in Dr:
    print(repr(x))
print_matrix("right_T", Tr)
print(f"det_sign {sign}")
print("G")
for j in range(n):
    for i in range(n):
        print(mp.nstr(G[i, j], 17))
