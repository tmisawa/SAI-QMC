#!/usr/bin/env python3
"""Compare conditional and ordinary estimators using independent replicas.

No observations are trimmed. A PT ladder is one independent unit at each
temperature; its slots are correlated. Reported SEs are descriptive sample
standard errors, not a validation of equilibration or tail/interval coverage.
Only Python's standard library is required.
"""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


def summary(values):
    return {"mean": statistics.mean(values),
            "se": statistics.stdev(values) / math.sqrt(len(values)) if len(values) > 1 else None,
            "replicas": len(values)}


def analyze(path, expect_replicas=None, expect_slots=None):
    text = path.read_text()
    lines = text.splitlines()
    headers = [s.removeprefix("# columns: ").split() for s in lines if s.startswith("# columns:")]
    if len(headers) != 1 or "# conditional_measure=1" not in text:
        raise ValueError("expected one conditional replica-bin header")
    metadata = next(s for s in lines if s.startswith("# lattice="))
    fields = dict(re.findall(r"(\w+)=([^\s]+)", metadata))
    n, U, nb, nm = int(fields["n"]), float(fields["U"]), int(fields["nbin"]), int(fields["nmeas"])
    if n <= 0 or not math.isfinite(U) or U < 0 or nb < 2 or nm < nb or nm % nb:
        raise ValueError("invalid model/bin metadata")
    qline = next(s for s in lines if s.startswith("# szz_Q_index="))
    qindices = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", qline)}
    groups = defaultdict(list)
    for line in lines:
        if not line or line.startswith("#"):
            continue
        row = dict(zip(headers[0], line.split(), strict=True))
        slot, replica = int(row["beta_index"]), int(row["replica_id"])
        count = int(row["count"])
        if count != nm // nb or int(row["conditional_count"]) != count or float(row["sum_sign"]) != count:
            raise ValueError("incomplete or non-sign-free bin")
        bid = int(row["bin_id"])
        if int(row["sweep_begin"]) != bid * count + 1 or int(row["sweep_end"]) != (bid + 1) * count:
            raise ValueError("invalid measurement sweep range")
        for key in ["sum_D_cond", "sum_K_cond", "sum_Ehub_cond", "sum_sign_D", "sum_sign_Ehub"]:
            if not math.isfinite(float(row[key])):
                raise ValueError(f"non-finite {key}")
        for key, index in [("sum_sign_Szz_Q", "szz_Q_index"),
                           ("sum_sign_Sperp_Q", "sperp_Q_index"),
                           ("sum_sign_Szz_0", "szz_0_index")]:
            value = float(row[key])
            if (qindices[index] >= 0 and not math.isfinite(value)) or (qindices[index] < 0 and not math.isnan(value)):
                raise ValueError(f"spin data disagree with measured momenta: {key}")
        e = float(row["sum_Ehub_cond"])
        expected = float(row["sum_K_cond"]) + U * n * float(row["sum_D_cond"])
        if not math.isclose(e, expected, rel_tol=1e-10, abs_tol=1e-10):
            raise ValueError("conditional E != K + U*N*D")
        groups[(slot, replica)].append(row)
    if not groups:
        raise ValueError("no data")
    slots = sorted({s for s, _ in groups})
    if slots != list(range(expect_slots if expect_slots is not None else max(slots) + 1)):
        raise ValueError("missing temperature slots")
    replicas = sorted(r for s, r in groups if s == 0)
    if replicas != list(range(expect_replicas if expect_replicas is not None else max(replicas) + 1)):
        raise ValueError("missing replica IDs")
    result = []
    for slot in slots:
        if sorted(r for s, r in groups if s == slot) != replicas:
            raise ValueError("inconsistent replica sets across slots")
        series = defaultdict(list)
        bin_D, bin_cond_D = [], []
        seeds, beta, Ltr = [], None, None
        for rid in replicas:
            rows = sorted(groups[(slot, rid)], key=lambda r: int(r["bin_id"]))
            if [int(r["bin_id"]) for r in rows] != list(range(nb)):
                raise ValueError("missing or duplicated bins")
            for key in ["seed", "beta_effective", "beta_requested", "Ltr"]:
                if len({r[key] for r in rows}) != 1:
                    raise ValueError(f"inconsistent {key}")
            b, lt = float(rows[0]["beta_effective"]), int(rows[0]["Ltr"])
            if not math.isfinite(b) or b <= 0 or lt <= 0 or (beta is not None and (beta != b or Ltr != lt)):
                raise ValueError("inconsistent temperature/time grid")
            beta, Ltr = b, lt
            seeds.append(int(rows[0]["seed"]))
            def avg(key):
                return math.fsum(float(r[key]) for r in rows) / nm
            old_D, old_E = avg("sum_sign_D"), avg("sum_sign_Ehub") / n
            new_D, new_E = avg("sum_D_cond"), avg("sum_Ehub_cond") / n
            series["ordinary_D"].append(old_D)
            series["conditional_D"].append(new_D)
            series["ordinary_E_per_site"].append(old_E)
            series["conditional_E_per_site"].append(new_E)
            series["ordinary_K_per_site"].append(old_E - U * old_D)
            series["conditional_K_per_site"].append(avg("sum_K_cond") / n)
            series["paired_D_difference"].append(new_D - old_D)
            series["paired_E_per_site_difference"].append(new_E - old_E)
            z, p = avg("sum_sign_Szz_Q"), avg("sum_sign_Sperp_Q")
            if math.isfinite(z):
                series["Szz_Q"].append(z)
                series["Sperp_from_2Szz_Q"].append(2 * z)
                series["Stot_from_3Szz_Q"].append(3 * z)
            if math.isfinite(p):
                series["Sperp_Q"].append(p)
                if math.isfinite(z):
                    series["Stot_Q"].append(z + p)
                    series["SU2_residual_Sperp_over_2_minus_Szz"].append(p / 2 - z)
            bin_D.extend(float(r["sum_sign_D"]) / int(r["count"]) for r in rows)
            bin_cond_D.extend(float(r["sum_D_cond"]) / int(r["conditional_count"]) for r in rows)
        if len(set(seeds)) != len(seeds):
            raise ValueError("duplicate replica seeds")
        if any(len(v) != len(replicas) for v in series.values()):
            raise ValueError("partially missing spin measurements")
        result.append({"slot": slot, "beta": beta, "dtau": beta / Ltr, "seeds": seeds,
                       "statistics": {k: summary(v) for k, v in series.items()},
                       "replica_values": dict(series),
                       "bin_D": {"ordinary_min": min(bin_D), "conditional_min": min(bin_cond_D),
                                 "ordinary_negative": sum(v < 0 for v in bin_D),
                                 "conditional_negative": sum(v < 0 for v in bin_cond_D),
                                 "bins": len(bin_D)}})
    return {"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "nsite": n, "U": U, "nbin": nb, "measurements_per_replica": nm,
            "independent_replicas": len(replicas), "all_bins_retained": True, "slots": result,
            "limits": "Independent-replica sample SE; no proof of equilibration, coverage, or cost-normalized improvement. PT slots are correlated. SU(2) alternatives require equilibrium."}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("bins", type=Path)
    p.add_argument("--expect-replicas", type=int)
    p.add_argument("--expect-slots", type=int)
    args = p.parse_args()
    print(json.dumps(analyze(args.bins, args.expect_replicas, args.expect_slots), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
