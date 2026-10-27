"""Compare the ED-comparison DQMC runs with grand-canonical ED.

For each case and beta: E_hub (total), ntot (total), doublon (per site) from
the full-precision re-analysis of bins.tsv (scripts/analyze_sign_bins.py),
Szz(q) from szz.dat, and the sign table. z = (QMC - ED) / err at each dtau.
The dtau^2 combination (4 X(dtau/2) - X(dtau)) / 3 is a diagnostic of the
step dependence only; it is not adopted as a dtau -> 0 value.

Rule fixed before the runs: a (case, beta, observable) is consistent when
|z0| <= 3 for the dtau^2 diagnostic; finite-dtau |z| are reported separately.

usage: compare_ed.py RUNDIR EDDIR SAI_QMC_ROOT > comparison.json
"""
import json
import math
from pathlib import Path
import sys


def main():
    run, ed, root = Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3])
    sys.path.insert(0, str(root / "scripts"))
    import analyze_sign_bins as asb
    cases = [("L4_%s" % bc, "ed_L4_%s_mu0.json" % bc) for bc in ("periodic", "antiperiodic")]
    cases += [("sq4x2_%s_mu%s" % (bc, mu), "ed_sq4x2_%s_mu%s.json" % (bc, mu))
              for bc in ("periodic", "antiperiodic") for mu in (2, 1)]
    out = []
    for name, edfile in cases:
        ref = json.loads((ed / edfile).read_text())
        nsite = ref["sites"]
        per = {}
        for dt in ("0.05", "0.025"):
            d = run / f"{name}_dt{dt}"
            res = asb.analyze(d / "bins.tsv")
            szz = [l.split() for l in (d / "szz.dat").read_text().splitlines()
                   if l and not l.startswith("#")]
            per[dt] = (res, szz)
        for k, r in enumerate(ref["results"]):
            row = dict(case=name, beta=r["beta"], observables={})
            for dt in ("0.05", "0.025"):
                b = per[dt][0]["betas"][k]
                assert abs(b["beta_effective"] - r["beta"]) < 1e-12
                row["sign_dt" + dt] = [b["sign"], b["dsign"]]
                row["status_dt" + dt] = b["ratio_status"]
                row["n_bins_nonpositive_dt" + dt] = b["n_bins_nonpositive"]
            items = [("E_hub", r["E_hub"]), ("ntot", r["ntot"]), ("doublon", r["doublon_per_site"])]
            items += [("Szz(%d,%d)" % (q["mx"], q["my"]), q["value"]) for q in r["Szz"]]
            for key, exact in items:
                vals = {}
                for dt in ("0.05", "0.025"):
                    res, szz = per[dt]
                    if key.startswith("Szz"):
                        mx, my = (int(x) for x in key[4:-1].split(","))
                        q = [s for s in szz if float(s[0]) == r["beta"] and
                             int(s[4]) == mx and int(s[5]) == my][0]
                        vals[dt] = (float(q[10]), float(q[11]))
                    else:
                        vals[dt] = tuple(res["betas"][k]["observables"][key])
                (m1, e1), (m2, e2) = vals["0.05"], vals["0.025"]
                m0 = (4 * m2 - m1) / 3
                e0 = math.sqrt(16 * e2 ** 2 + e1 ** 2) / 3
                z = lambda m, e: (m - exact) / e if e > 0 else (0.0 if m == exact else math.inf)
                row["observables"][key] = dict(
                    ed=exact, dt005=[m1, e1, z(m1, e1)], dt0025=[m2, e2, z(m2, e2)],
                    dtau2=[m0, e0, z(m0, e0)], consistent=abs(z(m0, e0)) <= 3)
            out.append(row)
    json.dump(out, sys.stdout, indent=1)
    print()


if __name__ == "__main__":
    main()
