#!/usr/bin/env python3
"""End-to-end checks of general mu, square tprime and the pooled sign ratio.

Serial (default): atomic limit against the exact grand-canonical values,
U = 0 free fermions with tprime under every boundary pair (exact for each
sample), zero and negative bins, replica-bin re-analysis, the
nonpositive_total / nonpositive_loo statuses and raw-data errors through the
test-hook build, the PH-only combinations, output collisions, sign_file I/O
failures, and the extended headers. --parallel repeats the deterministic
cases with the OpenMP, MPI and hybrid builds (identical data rows, root-only
sign_file, error propagation to every rank without a hang).

Only Python's standard library is required.
"""
import argparse
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import analyze_sign_bins  # noqa: E402

MPIRUN = os.environ.get("MPIRUN", "mpirun")
FAILURES = []


def check(cond, msg):
    if not cond:
        FAILURES.append(msg)
        print("FAIL", msg)


def run(work, name, text, binary="dqmc", nranks=0, env=None, extra_files=()):
    d = work / name
    d.mkdir(parents=True, exist_ok=True)
    (d / "input.in").write_text(text)
    for src, dst in extra_files:
        shutil.copy(src, d / dst)
    exe = str(ROOT / binary)
    cmd = [MPIRUN, "-n", str(nranks), exe, "input.in"] if nranks else [exe, "input.in"]
    full_env = dict(os.environ, OMP_NUM_THREADS="2", **(env or {}))
    try:
        p = subprocess.run(cmd, cwd=d, capture_output=True, text=True, timeout=300, env=full_env)
    except subprocess.TimeoutExpired:
        check(False, f"{name}: timed out (hang)")
        return d, -1, "", ""
    return d, p.returncode, p.stdout, p.stderr


def data_rows(path):
    return [line for line in Path(path).read_text().splitlines() if line and not line.startswith("#")]


def scalar(d):
    """Scalar rows of observables.dat as dicts."""
    names = ("T E_hub dE_hub E_gc dE_gc E_ph dE_ph ntot dN doublon dD sign "
             "acceptance dAcceptance").split()
    out = []
    for line in data_rows(d / "observables.dat"):
        vals = [float(x) for x in line.split()]
        out.append(dict(zip(names, vals)))
    return out


def sign_rows(d):
    text = (d / "sign.dat").read_text().splitlines()
    cols = next(s for s in text if s.startswith("# columns:")).removeprefix("# columns:").split()
    return [dict(zip(cols, line.split())) for line in text if line and not line.startswith("#")]


def close(a, b, tol):
    return math.isfinite(a) and math.isfinite(b) and abs(a - b) <= tol


# relative rounding of the scalar columns (%.8g energies/doublon, %.6g ntot)
PRINTED = {"E_hub": 1e-7, "E_gc": 1e-7, "E_ph": 1e-7, "doublon": 1e-7, "ntot": 1e-5}


# ---------------------------------------------------------------- inputs
BASE = "nwarm=50\nstab=4\nseed=20261027\n"


def atomic_input(mu, parallel="serial", nrep=2):
    return (f"lattice=chain\nLx=1\nt=0\nU=4\nmu={mu}\ndtau=0.25\nbeta_list=1,2\n"
            f"nmeas=4000\nnbin=20\nnrep={nrep}\nparallel={parallel}\nsign_file=sign.dat\n{BASE}")


def free_input(Lx, Ly, bx, by, parallel="serial"):
    return (f"lattice=square\nLx={Lx}\nLy={Ly}\nbc_x={bx}\nbc_y={by}\nt=-1\ntprime=-0.2\n"
            f"U=0\nmu=-0.7\ndtau=0.25\nbeta_list=2\nnmeas=40\nnbin=4\nnrep=2\n"
            f"parallel={parallel}\nsign_file=sign.dat\n{BASE}")


def square_input(parallel="serial", extra=""):
    return ("lattice=square\nLx=4\nLy=2\nt=-1\ntprime=-0.2\nU=4\nmu=1\ndtau=0.1\n"
            "beta_list=1,2\nnmeas=200\nnbin=10\nnrep=3\nszz_q=all\nsperp_q=all\n"
            "spin_consistency_file=su2.dat\nreplica_bin_file=bins.tsv\nsign_file=sign.dat\n"
            f"parallel={parallel}\n{BASE}{extra}")


def triangle_input(extra=""):
    return ("lattice=file\nlatfile=triangle.txt\nU=4\nmu=2\ndtau=0.5\nbeta_list=2\n"
            f"nmeas=600\nnbin=600\nnrep=2\nreplica_bin_file=bins.tsv\nsign_file=sign.dat\n"
            f"{BASE}{extra}")


TRIANGLE = [(ROOT / "tests/fixtures/sign_triangle/triangle.txt", "triangle.txt")]


# ---------------------------------------------------------------- exact values
def atomic_exact(mu, beta, U=4.0):
    z = 1 + 2 * math.exp(beta * mu) + math.exp(beta * (2 * mu - U))
    n = (2 * math.exp(beta * mu) + 2 * math.exp(beta * (2 * mu - U))) / z
    D = math.exp(beta * (2 * mu - U)) / z
    return dict(ntot=n, doublon=D, E_hub=U * D)


def ring(L, bc):
    """Eigenvalues and site weights |phi(x)|^2 of the 1D adjacency."""
    if bc == "open":
        ms = range(1, L + 1)
        eig = [2 * math.cos(math.pi * m / (L + 1)) for m in ms]
        amp = [[2 / (L + 1) * math.sin(math.pi * m * (x + 1) / (L + 1)) ** 2 for x in range(L)]
               for m in ms]
        return eig, amp
    shift = 0.0 if bc == "periodic" else 0.5
    eig = [2 * math.cos(2 * math.pi * (m + shift) / L) for m in range(L)]
    return eig, [[1.0 / L] * L for _ in range(L)]


def free_exact(Lx, Ly, bx, by, t=-1.0, tp=-0.2, mu=-0.7, beta=2.0):
    ex, px = ring(Lx, bx)
    ey, py = ring(Ly, by)
    n = Lx * Ly
    E = N = 0.0
    dens = [0.0] * n
    for a, wa in zip(ex, px):
        for b, wb in zip(ey, py):
            e = t * (a + b) + tp * a * b
            f = 1.0 / (1.0 + math.exp(beta * (e - mu)))
            E += 2 * e * f
            N += 2 * f
            for y in range(Ly):
                for x in range(Lx):
                    dens[x + Lx * y] += wa[x] * wb[y] * f
    D = sum(d * d for d in dens) / n
    return dict(E_hub=E, ntot=N, doublon=D)


# ---------------------------------------------------------------- serial cases
def check_atomic(work, binary="dqmc", parallel="serial", nranks=0, tag="serial"):
    rows_by_mu = {}
    for mu in (0, 1, 2, 3.25, -1):
        d, rc, _, err = run(work, f"atomic_{tag}_{mu}", atomic_input(mu, parallel),
                            binary, nranks)
        check(rc == 0, f"atomic {tag} mu={mu}: exit {rc} {err[:200]}")
        if rc != 0:
            continue
        rows = scalar(d)
        rows_by_mu[mu] = (data_rows(d / "observables.dat"), data_rows(d / "sign.dat"))
        head = (d / "observables.dat").read_text().splitlines()[0]
        ph = 1 if mu == 2 else 0
        check(f" mu={mu if mu != 3.25 else '3.25'} " in head and f"ph_symmetric={ph}" in head,
              f"atomic {tag} mu={mu}: header {head}")
        for row, beta in zip(rows, (1.0, 2.0)):
            ex = atomic_exact(mu, beta)
            for key, ekey in (("ntot", "dN"), ("doublon", "dD"), ("E_hub", "dE_hub")):
                ok = abs(row[key] - ex[key]) <= 5 * row[ekey] + 2e-6 * (1 + abs(ex[key]))
                check(ok, f"atomic {tag} mu={mu} beta={beta} {key}: {row[key]} +- {row[ekey]} "
                          f"vs exact {ex[key]}")
        if mu == 0:
            # half filling would give ntot = 1: mu reached the propagation
            check(abs(rows[0]["ntot"] - 1.0) > 0.2, "atomic mu=0 looks half filled")
    return rows_by_mu


def check_free(work, binary="dqmc", parallel="serial", nranks=0, tag="serial"):
    out = {}
    cases = [(4, 4, a, b) for a in ("periodic", "antiperiodic", "open")
             for b in ("periodic", "antiperiodic", "open")]
    cases += [(4, 2, "periodic", "periodic"), (4, 2, "antiperiodic", "periodic")]
    for Lx, Ly, bx, by in cases:
        name = f"free_{tag}_{Lx}x{Ly}_{bx[:2]}{by[:2]}"
        d, rc, _, err = run(work, name, free_input(Lx, Ly, bx, by, parallel), binary, nranks)
        check(rc == 0, f"{name}: exit {rc} {err[:200]}")
        if rc != 0:
            continue
        row = scalar(d)[0]
        ex = free_exact(Lx, Ly, bx, by)
        for key in ("E_hub", "ntot", "doublon"):
            check(close(row[key], ex[key], PRINTED[key] * (1 + abs(ex[key]))),
                  f"{name} {key}: {row[key]} vs exact {ex[key]}")
        check(row["sign"] == 1.0, f"{name}: sign {row['sign']}")
        s = sign_rows(d)[0]
        check(s["ratio_status"] == "ok" and s["no_negative_observed"] == "1",
              f"{name}: sign row {s}")
        head = (d / "observables.dat").read_text().splitlines()[0]
        check("tprime=-0.20000000000000001 ph_symmetric=0 estimator=pooled_ratio" in head,
              f"{name}: header {head}")
        out[name] = (data_rows(d / "observables.dat"), data_rows(d / "sign.dat"))
    return out


def check_zero_negative_bins(work):
    d, rc, _, err = run(work, "bins_negative", triangle_input(), extra_files=TRIANGLE)
    check(rc == 0, f"negative single-measurement bins: exit {rc} {err[:200]}")
    if rc == 0:
        s = sign_rows(d)[0]
        check(int(s["n_bins_nonpositive"]) > 0 and s["ratio_status"] == "ok",
              f"negative bins: sign row {s}")
        row = scalar(d)[0]
        check(all(math.isfinite(row[k]) for k in ("E_hub", "ntot", "doublon")),
              "negative bins: non-finite observables")
        check("WARNING" not in err, "negative bins: unexpected warning")
    d, rc, _, err = run(work, "bins_zero", triangle_input().replace("nmeas=600", "nmeas=1200"),
                        extra_files=TRIANGLE)
    check(rc == 0, f"zero-sum bins: exit {rc} {err[:200]}")
    if rc == 0:
        rows = [line.split("\t") for line in data_rows(d / "bins.tsv")]
        check(any(float(r[10]) == 0.0 for r in rows), "zero-sum bins: no bin with sum_sign 0")
        res = analyze_sign_bins.analyze(d / "bins.tsv")["betas"][0]
        row = scalar(d)[0]
        check(close(res["observables"]["E_hub"][0], row["E_hub"], 1e-7 * abs(row["E_hub"])),
              "zero-sum bins: re-analysis differs")


def check_reanalysis(work, binary="dqmc", parallel="serial", nranks=0, tag="serial"):
    d, rc, _, err = run(work, f"square_{tag}", square_input(parallel), binary, nranks)
    check(rc == 0, f"square {tag}: exit {rc} {err[:200]}")
    if rc != 0:
        return None
    text = (d / "bins.tsv").read_text().splitlines()
    check(text[0] == "# replica_bin_schema=2 mu=1 tprime=-0.20000000000000001 ph_symmetric=0 "
                     "estimator=pooled_ratio nrep=3 nbeta=2", f"square {tag}: schema line {text[0]}")
    cols = next(s for s in text if s.startswith("# columns:"))
    check(cols.endswith("sum_sign_Szz_0\tsum_sign_N\tsum_sign_Egc"), f"square {tag}: columns {cols}")
    for f in ("szz.dat", "sperp.dat", "su2.dat"):
        lat = next(s for s in (d / f).read_text().splitlines() if s.startswith("# lattice="))
        check(lat.endswith("tprime=-0.20000000000000001 ph_symmetric=0 estimator=pooled_ratio"),
              f"square {tag}: {f} header {lat}")
    if tag != "serial":
        return d
    res = analyze_sign_bins.analyze(d / "bins.tsv")
    srows = sign_rows(d)
    rows = scalar(d)
    qidx = dict(re.findall(r"(\w+)=(-?\d+)", next(s for s in text if s.startswith("# szz_Q_index="))))
    szz = [line.split() for line in data_rows(d / "szz.dat")]
    sperp = [line.split() for line in data_rows(d / "sperp.dat")]
    for k, b in enumerate(res["betas"]):
        check(b["ratio_status"] == srows[k]["ratio_status"], f"reanalysis status beta {k}")
        for key in ("sign", "dsign", "f_neg", "replica_sign_mean", "replica_sign_se"):
            check(close(b[key], float(srows[k][key]), 1e-12), f"reanalysis {key} beta {k}")
        for key in ("count_total", "negative_count", "n_bins_nonpositive", "n_loo_nonpositive",
                    "n_replica_nonpositive"):
            check(int(b[key]) == int(float(srows[k][key])), f"reanalysis {key} beta {k}")
        for key, ekey in (("E_hub", "dE_hub"), ("ntot", "dN"), ("doublon", "dD"),
                          ("E_gc", "dE_gc"), ("E_ph", "dE_ph")):
            m, e = b["observables"][key]
            check(close(m, rows[k][key], PRINTED[key] * (1 + abs(m))), f"reanalysis {key} beta {k}")
            check(close(e, rows[k][ekey], 0.06 * e + 1e-12), f"reanalysis {ekey} beta {k}")
        for name, table, q in (("Szz_Q", szz, int(qidx["szz_Q_index"])),
                               ("Szz_0", szz, int(qidx["szz_0_index"])),
                               ("Sperp_Q", sperp, int(qidx["sperp_Q_index"]))):
            row = [r for r in table if int(r[3]) == q][k]
            m, e = b["observables"][name]
            check(close(m, float(row[10]), 1e-12 * (1 + abs(m))) and
                  close(e, float(row[11]), 1e-10 * (1 + e)), f"reanalysis {name} beta {k}")
        # rebinning and replica units keep the central values
        for opts in ((2, "bin"), (5, "bin"), (1, "replica")):
            r2 = analyze_sign_bins.analyze(d / "bins.tsv", *opts)["betas"][k]
            check(close(r2["observables"]["E_hub"][0], b["observables"]["E_hub"][0], 1e-12),
                  f"rebin {opts} changes the central value")
            check(r2["blocks"] == (3 if opts[1] == "replica" else 30 // opts[0]),
                  f"rebin {opts} block count {r2['blocks']}")
    try:
        analyze_sign_bins.analyze(d / "bins.tsv", 4, "bin")
        check(False, "rebin 4 of nbin 10 accepted")
    except analyze_sign_bins.BinFileError:
        pass
    # SU(2) residual 0.5 Sperp - Szz: paired jackknife over the same blocks,
    # not the quadrature of independent errors; restored by the ensemble
    su2 = [line.split() for line in data_rows(d / "su2.dat")]
    for row in su2:
        m, e = float(row[10]), float(row[11])
        check(abs(m) <= 5 * e + 1e-12, f"SU(2) residual {m} +- {e}")
    data = analyze_sign_bins.read_bins(d / "bins.tsv")
    for k in sorted(data["rows"]):
        blocks = analyze_sign_bins.make_blocks(data, data["rows"][k], 1, "bin")
        for b in blocks:
            b["A"]["delta"] = 0.5 * b["A"]["Sperp_Q"] - b["A"]["Szz_Q"]
        S = math.fsum(b["S"] for b in blocks)
        A = math.fsum(b["A"]["delta"] for b in blocks)
        paired = analyze_sign_bins.jackknife(blocks, A, S, lambda b: b["A"]["delta"],
                                             lambda b: b["S"])
        row = [r for r in su2 if int(r[3]) == int(qidx["szz_Q_index"])][k]
        check(close(float(row[10]), A / S, 1e-12) and close(float(row[11]), paired, 1e-10),
              f"SU(2) residual at Q beta {k}: {row[10:12]} vs paired {A / S} {paired}")
        zq = [r for r in szz if int(r[3]) == int(qidx["szz_Q_index"])][k]
        pq = [r for r in sperp if int(r[3]) == int(qidx["sperp_Q_index"])][k]
        quad = math.hypot(0.5 * float(pq[11]), float(zq[11]))
        check(abs(paired - quad) > 1e-3 * quad, f"SU(2) residual error equals the quadrature {quad}")
    # unknown schema and a legacy schema-1 file
    bad = d / "bad.tsv"
    bad.write_text((d / "bins.tsv").read_text().replace("replica_bin_schema=2", "replica_bin_schema=3"))
    check(subprocess.run([sys.executable, str(ROOT / "scripts/analyze_sign_bins.py"), str(bad)],
                         capture_output=True).returncode != 0, "unknown schema accepted")
    return d


def check_legacy_schema(work):
    d, rc, _, err = run(work, "legacy_bins", "lattice=chain\nLx=4\nt=-1\nU=4\ndtau=0.1\n"
                        "beta_list=1\nnmeas=40\nnbin=4\nnrep=2\nreplica_bin_file=bins.tsv\n" + BASE)
    check(rc == 0, f"legacy bins: exit {rc}")
    first = (d / "bins.tsv").read_text().splitlines()[0]
    check(first.startswith("# replica-bin sums;"), f"legacy bins first line {first}")
    res = analyze_sign_bins.analyze(d / "bins.tsv")
    row = scalar(d)[0]
    check(res["schema"] == 1 and "ntot" not in res["betas"][0]["observables"],
          "legacy schema read")
    check(close(res["betas"][0]["observables"]["E_hub"][0], row["E_hub"], 1e-7 * abs(row["E_hub"])),
          "legacy re-analysis E_hub")
    check(not (d / "sign.dat").exists(), "legacy run created sign.dat")


def rewrite_table(src, dst, edit):
    """Copy a replica-bin table, applying edit(rows) to its data rows (lists of fields)."""
    lines = Path(src).read_text().splitlines()
    cols = next(s for s in lines if s.startswith("# columns:")).removeprefix("# columns:").split()
    rows = [l.split("\t") for l in lines if l and not l.startswith("#")]
    rows = edit(rows, {c: i for i, c in enumerate(cols)})
    Path(dst).write_text("\n".join(l for l in lines if l.startswith("#")) + "\n" +
                         "".join("\t".join(r) + "\n" for r in rows))


def expect_table_error(path, what, *args, **kw):
    try:
        analyze_sign_bins.analyze(path, *args, **kw)
        check(False, f"table check: {what} accepted")
    except analyze_sign_bins.BinFileError:
        pass


def check_table_integrity(work):
    """Re-analysis rejects corrupted or incomplete tables (review R2, R3)."""
    src = work / "square_serial" / "bins.tsv"   # nrep=3, nbeta=2, nbin=10
    bad = work / "integrity"
    bad.mkdir(exist_ok=True)

    def setcol(col, value, pick=lambda r, c: True):
        def edit(rows, c):
            for r in rows:
                if pick(r, c):
                    r[c[col]] = value(r, c) if callable(value) else value
            return rows
        return edit

    first_bin = lambda r, c: r[c["bin_id"]] == "0"
    cases = [
        ("all E_hub nan", setcol("sum_sign_Ehub", "nan"), {}),
        ("one nan per replica, replica units", setcol("sum_sign_Ehub", "nan", first_bin),
         {"unit": "replica"}),
        ("one nan per replica, rebin 10", setcol("sum_sign_Ehub", "nan", first_bin), {"rebin": 10}),
        ("N inf", setcol("sum_sign_N", "inf", first_bin), {}),
        ("measured Szz_Q nan", setcol("sum_sign_Szz_Q", "nan"), {}),
        ("count off by one", setcol("count", "21", first_bin), {}),
        ("missing middle replica", lambda rows, c: [r for r in rows if r[c["replica_id"]] != "1"], {}),
        ("missing last replica", lambda rows, c: [r for r in rows if r[c["replica_id"]] != "2"], {}),
        ("missing last beta", lambda rows, c: [r for r in rows if r[c["beta_index"]] != "1"], {}),
        ("missing one bin", lambda rows, c: [r for r in rows if not (r[c["replica_id"]] == "0" and
                                                                     r[c["bin_id"]] == "3")], {}),
        ("one row with another beta", lambda rows, c: [rows[0][:c["beta_requested"]] + ["123", "123"] +
                                                       rows[0][c["beta_effective"] + 1:]] + rows[1:], {}),
        ("one row with another Ltr", setcol("Ltr", "999", lambda r, c: r[c["bin_id"]] == "5"), {}),
        ("duplicate seed", lambda rows, c: [r[:c["seed"]] + [rows[0][c["seed"]]] + r[c["seed"] + 1:]
                                            if r[c["replica_id"]] == "2" and r[c["beta_index"]] == "0"
                                            else r for r in rows], {}),
        ("empty table", lambda rows, c: [], {}),
    ]
    for k, (what, edit, kw) in enumerate(cases):
        dst = bad / f"case{k}.tsv"
        rewrite_table(src, dst, edit)
        expect_table_error(dst, what, **kw)
    expect_table_error(src, "nrep differing from the header", nrep=4)
    # the command line exits nonzero with an ERROR line
    p = subprocess.run([sys.executable, str(ROOT / "scripts/analyze_sign_bins.py"), str(bad / "case0.tsv")],
                       capture_output=True, text=True)
    check(p.returncode != 0 and "ERROR: non-finite sum_sign_Ehub" in p.stderr, "table check exit status")
    # complete tables still pass: given matching sizes, a pre-header table, unmeasured q
    check(len(analyze_sign_bins.analyze(src, nrep=3, nbeta=2)["betas"]) == 2, "matching sizes rejected")
    old = bad / "old_header.tsv"
    old.write_text(src.read_text().replace(" nrep=3 nbeta=2\n", "\n", 1))
    expect_table_error(old, "pre-header table without --nrep")
    check(len(analyze_sign_bins.analyze(old, nrep=3, nbeta=2)["betas"]) == 2, "pre-header table with sizes")
    d, rc, _, _ = run(work, "unmeasured_q", square_input().replace("szz_q=all\nsperp_q=all\n"
                                                                    "spin_consistency_file=su2.dat\n", ""))
    check(rc == 0, "unmeasured q run")
    res = analyze_sign_bins.analyze(d / "bins.tsv")
    check(all("Szz_Q" not in b["observables"] and "E_hub" in b["observables"] for b in res["betas"]),
          "unmeasured q columns")
    filled = bad / "unmeasured_filled.tsv"
    rewrite_table(d / "bins.tsv", filled, setcol("sum_sign_Szz_Q", "1.5", first_bin))
    expect_table_error(filled, "a value in an unmeasured q column")
    for col in ("sum_sign_Szz_Q", "sum_sign_Sperp_Q", "sum_sign_Szz_0"):
        for value in ("inf", "-inf"):
            rewrite_table(d / "bins.tsv", filled, setcol(col, value, first_bin))
            expect_table_error(filled, f"{value} in unmeasured {col}")
            p = subprocess.run([sys.executable, str(ROOT / "scripts/analyze_sign_bins.py"), str(filled)],
                               capture_output=True, text=True)
            check(p.returncode != 0 and "disagrees with the measured momenta" in p.stderr,
                  f"{value} in unmeasured {col}: CLI must fail")


def check_sign_output_toggle(work, binary="dqmc", parallel="serial", nranks=0, tag="serial"):
    """Explicit sign_file=none selects exact PH testing without writing a file."""
    triangle = work / "sign_toggle_triangle.txt"
    triangle.write_text("3\n0 -1e-13 -1e-13\n-1e-13 0 -1e-13\n-1e-13 -1e-13 0\n")
    # mu is deliberately omitted: sign_file is the only new key.
    text = ("lattice=file\nlatfile=tri.txt\nU=4e-13\ndtau=5e12\nbeta_list=2e13\n"
            "nmeas=2000\nnbin=20\nnrep=2\nreplica_bin_file=bins.tsv\n"
            f"parallel={parallel}\n{BASE}")
    outputs = {}
    for setting in ("none", "sign.dat"):
        d, rc, _, err = run(work, f"sign_toggle_{tag}_{setting}", text + f"sign_file={setting}\n",
                            binary, nranks, extra_files=[(triangle, "tri.txt")])
        check(rc == 0, f"sign toggle {tag} {setting}: exit {rc} {err[:200]}")
        if rc != 0:
            return None
        head = (d / "observables.dat").read_text().splitlines()[0]
        check("bipartite=0" in head and "ph_symmetric=0 estimator=pooled_ratio" in head,
              f"sign toggle {tag} {setting}: {head}")
        row = scalar(d)[0]
        check(abs(row["ntot"] - 3) > 0.05 and row["sign"] < 1,
              f"sign toggle {tag} {setting}: looks PH")
        check((d / "sign.dat").exists() == (setting != "none") and not (d / "none").exists(),
              f"sign toggle {tag} {setting}: file creation")
        check((d / "bins.tsv").read_text().startswith("# replica_bin_schema=2 "),
              f"sign toggle {tag} {setting}: missing extended schema")
        outputs[setting] = (data_rows(d / "observables.dat"), data_rows(d / "bins.tsv"))
    check(outputs["none"] == outputs["sign.dat"], f"sign toggle {tag}: physical data differ")
    if tag == "serial":
        for extra, msg in (("tempering=dtau_ladder\ntempering_ltr=4\n", "tempering=dtau_ladder requires"),
                           ("sweep_order=alternating\n", "sweep_order=alternating requires"),
                           ("conditional_measure=1\n", "conditional_measure=1 requires")):
            gated = text + "sign_file=none\n" + extra
            if "tempering" in extra:
                gated = gated.replace("dtau=5e12\n", "").replace("beta_list=2e13", "beta_list=1e13,2e13")
            g, rc, _, err = run(work, f"sign_toggle_gate_{extra.split('=')[0]}", gated,
                                extra_files=[(triangle, "tri.txt")])
            check(rc != 0 and msg in err and {p.name for p in g.iterdir()} == {"input.in", "tri.txt"},
                  f"sign toggle gate {extra.split('=')[0]}: exit {rc} {err[:200]}")
    return outputs["none"][0]


def check_unit_scaling(work):
    """The PH decision of a hopping file uses the propagated matrix (review R1).

    Hopping, U and mu scaled by 1e-13 and dtau, beta by 1e13 give the same
    dimensionless model; with mu and sign_file the 1e-13 elements must not be
    dropped from the bipartite test. Without the newer keys the reader keeps
    its legacy 1e-12 tolerance (byte compatibility)."""
    rows = {}
    for name, lam in (("unit_1", 1.0), ("unit_1e-13", 1e-13)):
        t = f"{-lam:.17g}"
        tri = [(f"3\n0 {t} {t}\n{t} 0 {t}\n{t} {t} 0\n")]
        d = work / name
        d.mkdir(exist_ok=True)
        (d / "tri.txt").write_text(tri[0])
        text = (f"lattice=file\nlatfile=tri.txt\nU={4 * lam:.17g}\nmu={2 * lam:.17g}\n"
                f"dtau={0.5 / lam:.17g}\nbeta_list={2 / lam:.17g}\nnmeas=2000\nnbin=20\nnrep=2\n"
                f"sign_file=sign.dat\n{BASE}")
        d, rc, _, err = run(work, name, text)
        check(rc == 0, f"{name}: exit {rc} {err[:200]}")
        if rc != 0:
            return
        head = (d / "observables.dat").read_text().splitlines()[0]
        check("bipartite=0" in head and "ph_symmetric=0 estimator=pooled_ratio" in head,
              f"{name}: header {head}")
        r = scalar(d)[0]
        r["E_hub"] /= lam
        rows[name] = r
        for extra, msg in (("tempering=dtau_ladder\ntempering_ltr=4\n", "tempering=dtau_ladder requires"),
                           ("sweep_order=alternating\n", "sweep_order=alternating requires"),
                           ("conditional_measure=1\nreplica_bin_file=bins.tsv\n", "conditional_measure=1 requires")):
            gated = text + extra
            if "tempering" in extra:
                gated = gated.replace(f"dtau={0.5 / lam:.17g}\n", "").replace(
                    f"beta_list={2 / lam:.17g}", f"beta_list={1 / lam:.17g},{2 / lam:.17g}")
            g, grc, _, gerr = run(work, name + "_gate", gated, extra_files=[(d / "tri.txt", "tri.txt")])
            check(grc != 0 and msg in gerr and not (g / "observables.dat").exists(),
                  f"{name}: gate {extra.split('=')[0]}: exit {grc} {gerr[:200]}")
    a, b = rows.get("unit_1"), rows.get("unit_1e-13")
    if a and b:
        check(a["sign"] == b["sign"], f"unit scaling changes the sign {a['sign']} {b['sign']}")
        for key in ("E_hub", "ntot", "doublon"):
            check(close(a[key], b[key], 1e-7 * (1 + abs(a[key]))), f"unit scaling changes {key}")
    # legacy keys only: the reader's tolerance is kept (documented scope)
    legacy = (f"lattice=file\nlatfile=tri.txt\nU=4e-13\ndtau=5e12\nbeta_list=1e13\n"
              f"nmeas=40\nnbin=4\n{BASE}")
    d, rc, _, _ = run(work, "unit_legacy", legacy, extra_files=[(work / "unit_1e-13" / "tri.txt", "tri.txt")])
    head = (d / "observables.dat").read_text().splitlines()[0] if rc == 0 else ""
    check(rc == 0 and "bipartite=1" in head and "ph_symmetric" not in head,
          f"legacy tolerance scope: exit {rc} {head}")
    # with the newer keys a hopping file must be exactly symmetric
    d = work / "asym"
    d.mkdir(exist_ok=True)
    (d / "asym.txt").write_text("2\n0 -1\n-1.0000000000001 0\n")
    d, rc, _, err = run(work, "asym", "lattice=file\nlatfile=asym.txt\nmu=1\nnmeas=40\nnbin=4\n" + BASE)
    check(rc != 0 and "is not exactly symmetric" in err and not (d / "observables.dat").exists(),
          f"asymmetric file with mu: exit {rc} {err[:200]}")


def check_status_hooks(work, binary="build/test-hooks/dqmc", nranks=0, tag="serial"):
    out = {}
    inp = square_input().replace("nrep=3", "nrep=1").replace("nbin=10", "nbin=2")
    cases = (("total", "flip:0:1", "nonpositive_total", 2), ("loo", "flip:0:2", "nonpositive_loo", 3))
    for name, inject, status, nbin in cases:
        text = inp.replace("nbin=2", f"nbin={nbin}").replace("nmeas=200", "nmeas=210")
        if nranks:
            text = text.replace("parallel=serial", "parallel=mpi")
        d, rc, _, err = run(work, f"hook_{tag}_{name}", text, binary, nranks,
                            env={"AFQMC_TEST_POOLED_INJECT": inject})
        check(rc == 0, f"hook {tag} {name}: exit {rc} {err[:300]}")
        if rc != 0:
            continue
        check("WARNING: sign-reweighted ratio not evaluable at beta=1" in err and
              f"ratio_status={status}" in err, f"hook {tag} {name}: warning {err[:300]}")
        rows = scalar(d)
        srows = sign_rows(d)
        check(len(rows) == 2 and len(srows) == 2, f"hook {tag} {name}: rows {len(rows)}")
        check(all(math.isnan(rows[0][k]) for k in ("E_hub", "dE_hub", "ntot", "dN", "doublon", "dD")),
              f"hook {tag} {name}: observables not NaN {rows[0]}")
        check(math.isfinite(rows[0]["sign"]) and math.isfinite(rows[0]["acceptance"]),
              f"hook {tag} {name}: sign/acceptance not kept")
        check(srows[0]["ratio_status"] == status and srows[1]["ratio_status"] == "ok",
              f"hook {tag} {name}: sign rows {srows}")
        check(all(math.isfinite(rows[1][k]) for k in ("E_hub", "ntot")), f"hook {tag} {name}: beta 2")
        for f in ("szz.dat", "sperp.dat", "su2.dat"):
            q = [line.split() for line in data_rows(d / f)]
            check(len(q) == 16 and all(r[10] == "nan" and r[11] == "nan" for r in q[:8]) and
                  all(r[10] != "nan" for r in q[8:]), f"hook {tag} {name}: {f} rows")
        out[name] = (data_rows(d / "observables.dat"), data_rows(d / "sign.dat"),
                     data_rows(d / "szz.dat"))
    for name, inject, msg in (("nan", "nan:1:0", "ERROR: non-finite raw sum_sign_Ehub"),
                              ("count", "count:0:1", "ERROR: invalid replica-bin sign sums or counts")):
        text = inp if not nranks else inp.replace("parallel=serial", "parallel=mpi")
        d, rc, _, err = run(work, f"hook_{tag}_{name}", text, binary, nranks,
                            env={"AFQMC_TEST_POOLED_INJECT": inject})
        check(rc != 0 and msg in err, f"hook {tag} {name}: exit {rc} {err[:300]}")
    return out


def check_gates(work):
    for name, extra, msg in (
            ("pt", "tempering=dtau_ladder\ntempering_ltr=4\n", "tempering=dtau_ladder requires"),
            ("alt", "sweep_order=alternating\n", "sweep_order=alternating requires"),
            ("cond", "conditional_measure=1\n", "conditional_measure=1 requires")):
        text = triangle_input(extra)
        if name == "pt":
            text = text.replace("dtau=0.5\n", "").replace("beta_list=2", "beta_list=1,2")
        d, rc, _, err = run(work, f"gate_{name}", text, extra_files=TRIANGLE)
        check(rc != 0 and msg in err, f"gate {name}: exit {rc} {err[:200]}")
        check(not (d / "observables.dat").exists() and not (d / "hopping_used.txt").exists() and
              not (d / "sign.dat").exists(), f"gate {name}: an output was opened")
    # odd periodic chain without any new key: non-PH, extended schema
    d, rc, _, err = run(work, "odd_chain", "lattice=chain\nLx=5\npbc=1\nt=-1\nU=4\ndtau=0.1\n"
                        "beta_list=1\nnmeas=40\nnbin=4\nnrep=2\nreplica_bin_file=bins.tsv\n" + BASE)
    check(rc == 0, f"odd chain: exit {rc} {err[:200]}")
    if rc == 0:
        head = (d / "observables.dat").read_text().splitlines()[0]
        check("bipartite=0" in head and "ph_symmetric=0 estimator=pooled_ratio" in head,
              f"odd chain header {head}")
        check((d / "bins.tsv").read_text().startswith("# replica_bin_schema=2 mu=2 tprime=0 "
                                                       "ph_symmetric=0 estimator=pooled_ratio "
                                                       "nrep=2 nbeta=1\n"),
              "odd chain bins schema")
        check("INFO: ph_symmetric=0" in err, "odd chain INFO line")
    d, rc, _, err = run(work, "odd_chain_alt", "lattice=chain\nLx=5\npbc=1\nt=-1\nU=4\ndtau=0.1\n"
                        "beta_list=1\nnmeas=40\nnbin=4\nsweep_order=alternating\n" + BASE)
    check(rc != 0 and "sweep_order=alternating requires" in err, "odd chain alternating accepted")
    # explicit mu = U/2: extended header, legacy estimator and identical rows
    plain = "lattice=chain\nLx=4\nt=-1\nU=4\ndtau=0.1\nbeta_list=1\nnmeas=40\nnbin=4\nnrep=2\n" + BASE
    d0, rc0, _, _ = run(work, "ph_plain", plain)
    d1, rc1, _, _ = run(work, "ph_mu", plain + "mu=2\n")
    check(rc0 == 0 and rc1 == 0, "PH explicit mu runs")
    head = (d1 / "observables.dat").read_text().splitlines()[0]
    check(" mu=2 " in head and "ph_symmetric=1 estimator=bin_ratio" in head, f"PH mu header {head}")
    check(data_rows(d0 / "observables.dat") == data_rows(d1 / "observables.dat"),
          "explicit mu=U/2 changes the PH rows")
    check("ph_symmetric" not in (d0 / "observables.dat").read_text(), "legacy header extended")


def check_collisions_and_io(work):
    for name, extra, target in (("output", "sign_file=observables.dat\n", "observables.dat"),
                                ("bins", "sign_file=bins.tsv\n", None),
                                ("input", "sign_file=input.in\n", None),
                                ("hopping", "sign_file=hopping_used.txt\n", None)):
        text = triangle_input().replace("sign_file=sign.dat\n", "") + extra
        d, rc, _, err = run(work, f"collide_{name}", text, extra_files=TRIANGLE)
        check(rc != 0 and ("collision" in err or "collides" in err),
              f"collision {name}: exit {rc} {err[:200]}")
        check((d / "input.in").read_text() == text, f"collision {name}: input overwritten")
        check(not (d / "hopping_used.txt").exists(), f"collision {name}: solver started")
    d, rc, _, err = run(work, "io_missing_dir", triangle_input().replace(
        "sign_file=sign.dat", "sign_file=no_such_dir/sign.dat"), extra_files=TRIANGLE)
    check(rc != 0 and "ERROR: cannot open sign_file no_such_dir/sign.dat" in err,
          f"sign_file open failure: exit {rc} {err[:200]}")
    check(not data_rows(d / "observables.dat") if (d / "observables.dat").exists() else True,
          "sign_file open failure: rows written")
    for name, var, msg in (("write", "AFQMC_TEST_SIGN_WRITE_FAIL", "failed to write sign_file"),
                           ("close", "AFQMC_TEST_SIGN_CLOSE_FAIL", "failed to close sign_file")):
        d, rc, _, err = run(work, f"io_{name}", triangle_input(), "build/test-hooks/dqmc",
                            env={var: "1"}, extra_files=TRIANGLE)
        check(rc != 0 and msg in err, f"sign_file {name} failure: exit {rc} {err[:200]}")


# ---------------------------------------------------------------- parallel
def check_parallel(work):
    for tag, binary, nranks, par in (("omp", "dqmc_omp", 0, "omp"), ("mpi2", "dqmc_mpi", 2, "mpi"),
                                     ("mpi3", "dqmc_mpi", 3, "mpi"), ("hybrid", "dqmc_hybrid", 2, "hybrid")):
        toggle = check_sign_output_toggle(work, binary, par, nranks, tag)
        check(toggle == data_rows(work / "sign_toggle_serial_none" / "observables.dat"),
              f"sign toggle {tag}: rows differ from serial")
        a = check_atomic(work, binary, par, nranks, tag)
        for mu, (rows, srows) in a.items():
            ref_rows = data_rows(work / f"atomic_serial_{mu}" / "observables.dat")
            check(rows == ref_rows, f"atomic {tag} mu={mu} rows differ from serial")
            check(srows == data_rows(work / f"atomic_serial_{mu}" / "sign.dat"),
                  f"atomic {tag} mu={mu} sign rows differ")
        d = check_reanalysis(work, binary, par, nranks, tag)
        if d is not None:
            for f in ("observables.dat", "sign.dat", "szz.dat", "sperp.dat", "su2.dat", "bins.tsv"):
                check(data_rows(d / f) == data_rows(work / "square_serial" / f),
                      f"square {tag}: {f} differs from serial")
            text = (d / "sign.dat").read_text()
            check(text.count("# sign_file_schema=1") == 1 and len(data_rows(d / "sign.dat")) == 2,
                  f"square {tag}: sign_file not written once by the root rank")
        f = check_free(work, binary, par, nranks, tag)
        for name, (rows, srows) in f.items():
            ref = name.replace(f"free_{tag}_", "free_serial_")
            check(rows == data_rows(work / ref / "observables.dat"), f"{name} rows differ")
    # statuses and raw-data errors under MPI: same rows as serial, every rank exits
    serial = check_status_hooks(work, "build/test-hooks/dqmc", 0, "serial_ref")
    for tag, binary, nranks in (("mpi", "build/test-hooks/dqmc_mpi", 3),
                                ("hybrid", "build/test-hooks/dqmc_hybrid", 2)):
        rows = check_status_hooks(work, binary, nranks, tag)
        for name in serial:
            check(rows.get(name) == serial[name], f"hook {tag} {name}: rows differ from serial")
    for tag, binary, nranks in (("mpi", "build/test-hooks/dqmc_mpi", 2),):
        for name, var, msg in (("write", "AFQMC_TEST_SIGN_WRITE_FAIL", "failed to write sign_file"),
                               ("close", "AFQMC_TEST_SIGN_CLOSE_FAIL", "failed to close sign_file")):
            d, rc, _, err = run(work, f"io_{tag}_{name}", square_input("mpi"), binary, nranks,
                                env={var: "1"})
            check(rc != 0 and msg in err, f"sign_file {name} failure {tag}: exit {rc} {err[:200]}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--parallel", action="store_true")
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()
    work = Path(tempfile.mkdtemp(prefix="sign_output_"))
    try:
        check_atomic(work)
        check_free(work)
        check_reanalysis(work)
        check_sign_output_toggle(work)
        if args.parallel:
            check_parallel(work)
        else:
            check_zero_negative_bins(work)
            check_table_integrity(work)
            check_unit_scaling(work)
            check_legacy_schema(work)
            check_status_hooks(work)
            check_gates(work)
            check_collisions_and_io(work)
    finally:
        if args.keep:
            print("kept", work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    if FAILURES:
        print(f"{len(FAILURES)} SIGN OUTPUT CHECK(S) FAILED")
        return 1
    print("SIGN OUTPUT CHECKS PASSED" + (" (parallel)" if args.parallel else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
