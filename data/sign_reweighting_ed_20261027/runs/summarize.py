"""Markdown summary of comparison.json (and the post-hoc dtau=0.0125 follow-up).

usage: summarize.py comparison.json [followup_rows.json] > analysis_output.md
"""
import json
import sys

rows = json.load(open(sys.argv[1]))
print("# DQMC versus ED: sign-reweighted estimator\n")
print("z = (QMC - ED) / err at dtau 0.05 and 0.025; z0 uses (4 X(0.025) - X(0.05)) / 3 "
      "as a diagnostic of the step dependence. Rule fixed before the runs: |z0| <= 3.\n")
print("| case | beta | sign (dtau 0.05) | sign (dtau 0.025) | E_hub z / z / z0 | ntot z / z / z0 | "
      "doublon z / z / z0 | Szz max abs z0 |")
print("| --- | ---: | ---: | ---: | --- | --- | --- | ---: |")
n_bad = n_all = 0
for r in rows:
    s1, s2 = r["sign_dt0.05"], r["sign_dt0.025"]
    cells = []
    for k in ("E_hub", "ntot", "doublon"):
        o = r["observables"][k]
        mark = "" if o["consistent"] else " **(>3)**"
        cells.append("%.1f / %.1f / %.1f%s" % (o["dt005"][2], o["dt0025"][2], o["dtau2"][2], mark))
    szz = max(abs(o["dtau2"][2]) for k, o in r["observables"].items() if k.startswith("Szz"))
    n_all += len(r["observables"])
    n_bad += sum(1 for o in r["observables"].values() if not o["consistent"])
    print("| %s | %g | %.4f(%.0e) | %.4f(%.0e) | %s | %.1f |" % (
        r["case"], r["beta"], s1[0], s1[1], s2[0], s2[1], " | ".join(cells), szz))
print("\n%d of %d comparisons satisfy |z0| <= 3; %d do not." % (n_all - n_bad, n_all, n_bad))
