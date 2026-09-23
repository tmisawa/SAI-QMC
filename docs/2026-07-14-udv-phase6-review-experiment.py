#!/usr/bin/env python3
"""Feasibility check for Phase 6 augmented cyclic solver design.

Questions:
 Q1. How does the accuracy of the dense augmented solve of
     G = (I + F[m-1]...F[0])^-1 depend on the per-chunk logD spread s?
     Design assumes chunk_beta_max=8 -> s ~ 320 (gate allows 600) is fine.
 Q2. Do diagonal chunks (design's "analytic overflow-scale" gate 3 case)
     exercise the same difficulty as rotation-mixed chunks?
 Q3. Is the normalized residual ||KX-RHS||/(||K|| ||X|| + ||RHS||) (gate 4)
     able to detect accuracy loss?
 Q4. Does a *centered* scalar exponent offset make the existing one-factor
     Db/Ds inversion work in plain double at the beta=33.325 target spread
     (~1200-1330)?
"""
import numpy as np
import mpmath as mp

mp.mp.dps = 500
rng = np.random.default_rng(20260714)


def rand_orth(n):
    q, r = np.linalg.qr(rng.standard_normal((n, n)))
    return q * np.sign(np.diag(r))


def make_chain(n, m, spread, mixed=True):
    """m chunks, each with logD spread `spread` centered at 0."""
    Fs = []
    for _ in range(m):
        lam = np.linspace(spread / 2, -spread / 2, n)
        lam += rng.uniform(-1, 1, n)
        D = np.exp(lam)
        if mixed:
            F = rand_orth(n) @ np.diag(D) @ rand_orth(n)
        else:
            F = np.diag(D)
        Fs.append(F)
    return Fs


def ref_green(Fs):
    n = Fs[0].shape[0]
    P = mp.eye(n)
    for F in Fs:  # product F[m-1]...F[0]
        P = mp.matrix(F.tolist()) * P
    G = (mp.eye(n) + P) ** -1
    return np.array([[float(G[i, j]) for j in range(n)] for i in range(n)])


def build_K(Fs):
    m = len(Fs)
    n = Fs[0].shape[0]
    K = np.zeros((m * n, m * n))
    for k in range(m):
        K[k * n:(k + 1) * n, k * n:(k + 1) * n] = np.eye(n)
    for k in range(1, m):
        K[k * n:(k + 1) * n, (k - 1) * n:k * n] = -Fs[k - 1]
    K[0:n, (m - 1) * n:m * n] = Fs[m - 1]
    return K


def solve_aug(Fs, equilibrate=False):
    m = len(Fs)
    n = Fs[0].shape[0]
    K = build_K(Fs)
    rhs = np.zeros((m * n, n))
    rhs[:n, :n] = np.eye(n)
    try:
        if equilibrate:
            r = 1.0 / np.abs(K).max(axis=1)
            Kr = K * r[:, None]
            c = 1.0 / np.abs(Kr).max(axis=0)
            Krc = Kr * c[None, :]
            y = np.linalg.solve(Krc, rhs * r[:, None])
            X = y * c[:, None]
        else:
            X = np.linalg.solve(K, rhs)
    except np.linalg.LinAlgError:
        return None, float("inf"), K
    G = X[:n, :]
    # design's gate-4 normalized residual (on the unscaled system)
    R = K @ X - rhs
    resid = np.abs(R).max() / (np.abs(K).max() * np.abs(X).max()
                               + np.abs(rhs).max())
    return G, resid, K


def relerr(G, Gref):
    if G is None or not np.isfinite(G).all():
        return float("inf")  # LU declared singular / non-finite result
    return np.abs(G - Gref).max() / np.abs(Gref).max()


print("=== Q1/Q2/Q3: augmented dense solve accuracy vs per-chunk spread ===")
print("n=4, m=4 chunks; 'mixed' = orthogonal-rotated chunks (DQMC-like),")
print("'diag' = diagonal chunks (design's analytic gate case)")
hdr = (f"{'spread':>7} {'err_mixed':>10} {'err_equil':>10} {'err_diag':>10} "
       f"{'gate4resid':>10} {'rcond(K)':>10}")
print(hdr)
for spread in [10, 20, 30, 40, 60, 80, 120, 160, 240, 320]:
    errs = {"mixed": [], "equil": [], "diag": [], "resid": [], "rc": []}
    for trial in range(3):
        Fs = make_chain(4, 4, spread, mixed=True)
        Gref = ref_green(Fs)
        G, resid, K = solve_aug(Fs)
        Ge, _, _ = solve_aug(Fs, equilibrate=True)
        errs["mixed"].append(relerr(G, Gref))
        errs["equil"].append(relerr(Ge, Gref))
        errs["resid"].append(resid)
        try:
            c = np.linalg.cond(K)
            rc = 1.0 / c if np.isfinite(c) else 0.0
        except Exception:
            rc = float("nan")
        errs["rc"].append(rc)
        Fd = make_chain(4, 4, spread, mixed=False)
        Gdref = ref_green(Fd)
        Gd, _, _ = solve_aug(Fd)
        errs["diag"].append(relerr(Gd, Gdref))
    print(f"{spread:7d} {np.median(errs['mixed']):10.2e} "
          f"{np.median(errs['equil']):10.2e} {np.median(errs['diag']):10.2e} "
          f"{np.median(errs['resid']):10.2e} {np.median(errs['rc']):10.2e}")

print()
print("=== det(K) = det(I+P) sign check (m odd/even, negative det) ===")
for m in [2, 3, 4, 5]:
    Fs = make_chain(3, m, 6, mixed=True)
    K = build_K(Fs)
    P = np.eye(3)
    for F in Fs:
        P = F @ P
    sK = np.sign(np.linalg.det(K))
    sIP = np.sign(np.linalg.det(np.eye(3) + P))
    print(f"  m={m}: sign det(K)={sK:+.0f}, sign det(I+P)={sIP:+.0f}, "
          f"match={sK == sIP}")

print()
print("=== Q4: centered-offset single factor + Db/Ds inversion in double ===")
print("G = (I + U D T)^-1 with D spread ~1200 (beta=33.325-like), centered")


def inv_one_plus_dbds(U, lam, T):
    """Loh et al. style: G = T^-1 M^-1 Db^-1 U^T,
    M = Db^-1 U^T T^-1 + Ds, split D = Db*Ds at |d|=1 (log split at 0)."""
    n = len(lam)
    big = lam > 0
    Dbinv = np.where(big, np.exp(-lam), 1.0)
    Ds = np.where(big, 1.0, np.exp(lam))
    Tinv = np.linalg.inv(T)
    M = (Dbinv[:, None] * (U.T @ Tinv)) + np.diag(Ds)
    G = Tinv @ np.linalg.solve(M, Dbinv[:, None] * U.T)
    return G


for spread, label in [(1200, "beta~33 target"), (1330, "beta~33 upper est"),
                      (1400, "near double wall")]:
    n = 8
    lam = np.linspace(spread / 2, -spread / 2, n) + rng.uniform(-1, 1, n)
    U = rand_orth(n)
    T = rand_orth(n)  # stand-in for well-conditioned T factor
    G = inv_one_plus_dbds(U, lam, T)
    # mpmath reference
    Dm = mp.diag([mp.e ** mp.mpf(x) for x in lam])
    Pm = mp.matrix(U.tolist()) * Dm * mp.matrix(T.tolist())
    Gref = (mp.eye(n) + Pm) ** -1
    Grefn = np.array([[float(Gref[i, j]) for j in range(n)] for i in range(n)])
    print(f"  spread={spread:5d} ({label}): finite={np.isfinite(G).all()}, "
          f"rel err vs mpmath = {relerr(G, Grefn):.2e}")

print()
print("=== Q4b: max-normalized offset (design's implicit convention) ===")
for spread in [700, 800, 1200]:
    n = 8
    lam = np.linspace(0, -spread, n)  # max normalized to 1
    D = np.exp(lam)
    n_under = int((D == 0.0).sum())
    print(f"  spread={spread}: max-normalized D has {n_under}/{n} entries "
          f"underflow to exactly 0 -> rank loss (design's 745-wall claim)")
