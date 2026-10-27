#!/usr/bin/env python3
"""Sign-reweighted re-analysis of a replica_bin_file.

For every beta point the replica-bin blocks are pooled exactly as the solver
does for a model without particle-hole symmetry:

    R = sum_b A_b / S,  S = sum_b S_b,  R(-b) = (A - A_b) / (S - S_b),
    err^2 = (B - 1)/B sum_b (R(-b) - mean R(-b))^2,

with the sign s = S / C, f_neg = (1 - s)/2 and ratio_status ok,
nonpositive_total (S <= 0) or nonpositive_loo (some S - S_b <= 0). Blocks
with a zero or negative sign sum are never dropped; unless the status is ok
the observables and their errors are NaN.

--rebin K merges K consecutive bins inside each replica (nbin must be a
multiple of K; no block is discarded) and --unit replica pools each replica
into one block (replica jackknife from the total raw sums). The replica sign
mean includes non-positive replicas.

Every row is validated before any merging. Counts, sign sums and the energy,
doublon (and, in schema 2, N and E_gc) sums must be finite; a q column must be
finite when the '# szz_Q_index=...' line marks it measured and NaN otherwise.
Within a beta point all rows share beta_requested, beta_effective and Ltr,
every replica has one seed, distinct from the others, and bins 0..nbin-1
with their sweep ranges. An empty table is an error.

Schema 1 (legacy) files carry sum_sign, sum_sign_Ehub, sum_sign_D and the
selected q columns, but not the number of replicas or beta points: only
contiguous replica and beta indices starting at 0 are checked, and a missing
last replica or beta point is detected only with --nrep / --nbeta. Schema 2
files ("# replica_bin_schema=2 ...") record nrep and nbeta (files written
before that header field existed need --nrep and --nbeta), add sum_sign_N
and sum_sign_Egc, and E_ph is rebuilt as
sum_sign_Ehub - (U/2) sum_sign_N + U nsite sum_sign / 4. Other schemas are
rejected. Only Python's standard library is required.

usage: analyze_sign_bins.py BINS.tsv [--rebin K] [--unit bin|replica]
                            [--nrep N] [--nbeta K] [--json]
"""
import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import re
import sys

STATUS_OK, STATUS_TOTAL, STATUS_LOO = "ok", "nonpositive_total", "nonpositive_loo"
Q_COLUMNS = (("sum_sign_Szz_Q", "szz_Q_index"), ("sum_sign_Sperp_Q", "sperp_Q_index"),
             ("sum_sign_Szz_0", "szz_0_index"))
INT_COLUMNS = ("beta_index", "Ltr", "replica_id", "seed", "bin_id", "sweep_begin",
               "sweep_end", "count", "local_accepted", "local_attempts",
               "global_accepted", "global_attempts")


class BinFileError(ValueError):
    pass


def _int(row, key):
    try:
        v = int(row[key])
    except ValueError:
        raise BinFileError(f"{key}={row[key]!r} is not an integer") from None
    if v < 0:
        raise BinFileError(f"{key}={v} is negative")
    return v


def _float(row, key, finite=True):
    try:
        v = float(row[key])
    except ValueError:
        raise BinFileError(f"{key}={row[key]!r} is not a number") from None
    if finite and not math.isfinite(v):
        raise BinFileError(f"non-finite {key} in beta_index={row['beta_index']} "
                           f"replica={row['replica_id']} bin={row['bin_id']}")
    return v


def read_bins(path, nrep=None, nbeta=None):
    lines = Path(path).read_text().splitlines()
    schema, meta = 1, {}
    for line in lines:
        m = re.match(r"# replica_bin_schema=(\S+)(.*)$", line)
        if m:
            if m.group(1) != "2":
                raise BinFileError(f"unknown replica_bin_schema={m.group(1)}")
            schema = 2
            meta = dict(re.findall(r"(\w+)=(\S+)", m.group(2)))
            for key in ("mu", "tprime", "ph_symmetric", "estimator"):
                if key not in meta:
                    raise BinFileError(f"schema 2 header lacks {key}")
            # files written before the header recorded nrep/nbeta need them given
            for key, given in (("nrep", nrep), ("nbeta", nbeta)):
                if key not in meta:
                    if given is None:
                        raise BinFileError(f"schema 2 header lacks {key}; give --{key}")
                    meta[key] = str(given)
    columns = [s.removeprefix("# columns:").split() for s in lines if s.startswith("# columns:")]
    lattice = [s for s in lines if s.startswith("# lattice=")]
    qline = [s for s in lines if s.startswith("# szz_Q_index=")]
    if len(columns) != 1 or len(lattice) != 1 or len(qline) != 1:
        raise BinFileError("expected one '# columns:', '# lattice=' and '# szz_Q_index=' line")
    fields = dict(re.findall(r"(\w+)=(\S+)", lattice[0]))
    nsite, U, nbin, nmeas = (int(fields["n"]), float(fields["U"]),
                             int(fields["nbin"]), int(fields["nmeas"]))
    dtau = float(fields["dtau"])
    if nsite <= 0 or nbin < 2 or nmeas < nbin or nmeas % nbin:
        raise BinFileError("invalid n/nbin/nmeas metadata")
    measured = {col: int(v) >= 0 for col, key in Q_COLUMNS
                for k, v in re.findall(r"(\w+)=(-?\d+)", qline[0]) if k == key}
    if set(measured) != {c for c, _ in Q_COLUMNS}:
        raise BinFileError("incomplete '# szz_Q_index=' line")
    if schema == 2:
        for name, given in (("nrep", nrep), ("nbeta", nbeta)):
            value = int(meta[name])
            if value < 1 or (given is not None and given != value):
                raise BinFileError(f"{name}={value} in the header, expected {given}")
        nrep, nbeta = int(meta["nrep"]), int(meta["nbeta"])
        if not {"sum_sign_N", "sum_sign_Egc"} <= set(columns[0]):
            raise BinFileError("schema 2 without sum_sign_N/sum_sign_Egc columns")
    elif "sum_sign_N" in columns[0]:
        raise BinFileError("sum_sign_N column without a schema 2 header")
    required = ["sum_sign", "sum_sign_Ehub", "sum_sign_D"]
    if schema == 2:
        required += ["sum_sign_N", "sum_sign_Egc"]
    if "conditional_count" in columns[0]:
        required += ["sum_D_cond", "sum_K_cond", "sum_Ehub_cond"]
    data = dict(schema=schema, meta=meta, nsite=nsite, U=U, nbin=nbin, nmeas=nmeas,
                columns=columns[0], measured=measured, rows=defaultdict(list))
    for line in lines:
        if not line or line.startswith("#"):
            continue
        values = line.split("\t")
        if len(values) != len(columns[0]):
            raise BinFileError("row width differs from the column header")
        row = dict(zip(columns[0], values))
        for key in INT_COLUMNS:
            row[key] = _int(row, key)
        for key in ("beta_requested", "beta_effective") + tuple(required):
            row[key] = _float(row, key)
        for col, _ in Q_COLUMNS:
            v = _float(row, col, finite=False)
            valid = math.isfinite(v) if measured[col] else math.isnan(v)
            if not valid:
                raise BinFileError(f"{col}={row[col]} in beta_index={row['beta_index']} "
                                   f"replica={row['replica_id']} bin={row['bin_id']} "
                                   f"disagrees with the measured momenta")
            row[col] = v
        data["rows"][row["beta_index"]].append(row)
    if not data["rows"]:
        raise BinFileError("the table has no data rows")
    check_complete(data, nrep, nbeta, dtau)
    return data


def _indices(found, expected, what):
    found = sorted(found)
    if expected is not None:
        if found != list(range(expected)):
            missing = sorted(set(range(expected)) - set(found))
            extra = sorted(set(found) - set(range(expected)))
            raise BinFileError(f"{what}: expected 0..{expected - 1}, missing {missing}, unexpected {extra}")
    elif found != list(range(len(found))):
        raise BinFileError(f"{what}: indices {found} are not 0..{len(found) - 1}")


def check_complete(data, nrep, nbeta, dtau):
    nbin = data["nbin"]
    per = data["nmeas"] // nbin
    _indices(data["rows"], nbeta, "beta points")
    for k, rows in data["rows"].items():
        first = rows[0]
        for r in rows:
            for key in ("beta_requested", "beta_effective", "Ltr"):
                if r[key] != first[key]:
                    raise BinFileError(f"beta_index={k}: {key} {r[key]} differs from {first[key]}")
        if math.isfinite(dtau) and abs(first["beta_effective"] - first["Ltr"] * dtau) > \
                1e-9 * max(1.0, first["beta_effective"]):
            raise BinFileError(f"beta_index={k}: beta_effective is not Ltr*dtau")
        by_replica = defaultdict(list)
        for r in rows:
            by_replica[r["replica_id"]].append(r)
        _indices(by_replica, nrep, f"beta_index={k} replicas")
        seeds = {}
        for rid, reps in by_replica.items():
            if len({r["seed"] for r in reps}) != 1:
                raise BinFileError(f"beta_index={k} replica {rid} has several seeds")
            seeds[rid] = reps[0]["seed"]
            if sorted(r["bin_id"] for r in reps) != list(range(nbin)):
                raise BinFileError(f"beta_index={k} replica {rid} does not have bins 0..{nbin - 1} once")
            for r in reps:
                b = r["bin_id"]
                if r["count"] != per or r["sweep_begin"] != b * per + 1 or r["sweep_end"] != (b + 1) * per:
                    raise BinFileError(f"beta_index={k} replica {rid} bin {b}: count or sweep range")
                s = r["sum_sign"]
                if s != math.floor(s) or abs(s) > per or (per - int(s)) % 2:
                    raise BinFileError(f"beta_index={k} replica {rid} bin {b}: invalid sum_sign {s}")
        if len(set(seeds.values())) != len(seeds):
            raise BinFileError(f"beta_index={k}: two replicas share a seed")
    if data["schema"] == 1 and (nrep is None or nbeta is None):
        print("WARNING: schema 1 records neither nrep nor nbeta; a missing last replica or "
              "beta point is detected only with --nrep/--nbeta", file=sys.stderr)


def numerators(data, row):
    S = row["sum_sign"]
    out = {"E_hub": row["sum_sign_Ehub"], "doublon": row["sum_sign_D"]}
    if data["schema"] == 2:
        N = row["sum_sign_N"]
        out.update(ntot=N, E_gc=row["sum_sign_Egc"],
                   E_ph=out["E_hub"] - 0.5 * data["U"] * N + 0.25 * data["U"] * data["nsite"] * S)
    for col, _ in Q_COLUMNS:
        if data["measured"][col]:
            out[col.removeprefix("sum_sign_")] = row[col]
    return out


def make_blocks(data, rows, rebin, unit):
    nbin = data["nbin"]
    by_replica = defaultdict(list)
    for row in rows:
        by_replica[row["replica_id"]].append(row)
    group = nbin if unit == "replica" else rebin
    if nbin % group:
        raise BinFileError(f"nbin={nbin} is not a multiple of --rebin {group}")
    blocks = []
    for rid in sorted(by_replica):
        reps = sorted(by_replica[rid], key=lambda r: r["bin_id"])
        for start in range(0, nbin, group):
            part = reps[start:start + group]
            nums = defaultdict(list)
            for r in part:
                for key, v in numerators(data, r).items():
                    nums[key].append(v)
            blocks.append(dict(replica=rid, count=sum(r["count"] for r in part),
                               S=math.fsum(r["sum_sign"] for r in part),
                               A={k: math.fsum(v) for k, v in nums.items()}))
    return blocks


def jackknife(blocks, total_num, total_den, num, den):
    B = len(blocks)
    loo = [(total_num - num(b)) / (total_den - den(b)) for b in blocks]
    mean = sum(loo) / B
    return math.sqrt((B - 1) / B * sum((x - mean) ** 2 for x in loo))


def analyze_beta(data, rows, rebin=1, unit="bin"):
    blocks = make_blocks(data, rows, rebin, unit)
    if len(blocks) < 2:
        raise BinFileError("at least two blocks are needed for the jackknife")
    C = sum(b["count"] for b in blocks)
    S = math.fsum(b["S"] for b in blocks)
    n_loo = sum(1 for b in blocks if S - b["S"] <= 0)
    status = STATUS_TOTAL if S <= 0 else STATUS_LOO if n_loo else STATUS_OK
    sign = S / C
    dsign = jackknife(blocks, S, C, lambda b: b["S"], lambda b: b["count"])
    replicas = defaultdict(lambda: [0.0, 0])
    for b in blocks:
        replicas[b["replica"]][0] += b["S"]
        replicas[b["replica"]][1] += b["count"]
    rsigns = [s / c for s, c in replicas.values()]
    rmean = sum(rsigns) / len(rsigns)
    rse = (math.sqrt(sum((x - rmean) ** 2 for x in rsigns) / (len(rsigns) - 1) / len(rsigns))
           if len(rsigns) > 1 else math.nan)
    out = dict(beta_index=rows[0]["beta_index"], beta_requested=rows[0]["beta_requested"],
               beta_effective=rows[0]["beta_effective"], unit=unit, rebin=rebin,
               blocks=len(blocks), count_total=C, sum_sign_total=S,
               negative_count=(C - int(S)) // 2, sign=sign, dsign=dsign,
               f_neg=0.5 * (1 - sign), df_neg=0.5 * dsign,
               replica_sign_mean=rmean, replica_sign_se=rse,
               n_replica_nonpositive=sum(1 for s, _ in replicas.values() if s <= 0),
               n_bins_nonpositive=sum(1 for b in blocks if b["S"] <= 0),
               n_loo_nonpositive=n_loo, ratio_status=status,
               sign_unresolved=int(0 < sign <= 2 * dsign), observables={})
    for key in blocks[0]["A"]:
        A = math.fsum(b["A"][key] for b in blocks)
        if status != STATUS_OK:
            out["observables"][key] = [math.nan, math.nan]
            continue
        err = jackknife(blocks, A, S, lambda b, k=key: b["A"][k], lambda b: b["S"])
        if not (math.isfinite(A / S) and math.isfinite(err)):
            raise BinFileError(f"non-finite {key} ratio at beta_index={out['beta_index']}")
        out["observables"][key] = [A / S, err]
    return out


def analyze(path, rebin=1, unit="bin", nrep=None, nbeta=None):
    data = read_bins(path, nrep, nbeta)
    return dict(schema=data["schema"], metadata=data["meta"],
                betas=[analyze_beta(data, data["rows"][k], rebin, unit) for k in sorted(data["rows"])])


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("bins")
    ap.add_argument("--rebin", type=int, default=1)
    ap.add_argument("--unit", choices=("bin", "replica"), default="bin")
    ap.add_argument("--nrep", type=int, help="expected replicas (checked against schema 2)")
    ap.add_argument("--nbeta", type=int, help="expected beta points (checked against schema 2)")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    if args.rebin < 1:
        ap.error("--rebin must be >= 1")
    try:
        result = analyze(args.bins, args.rebin, args.unit, args.nrep, args.nbeta)
    except (BinFileError, KeyError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if args.json:
        json.dump(result, sys.stdout, indent=1)
        print()
        return 0
    print(f"# schema={result['schema']} unit={args.unit} rebin={args.rebin}")
    for b in result["betas"]:
        print(f"beta_index={b['beta_index']} beta={b['beta_effective']:.17g} "
              f"sign={b['sign']:.17g} dsign={b['dsign']:.3g} f_neg={b['f_neg']:.6g} "
              f"status={b['ratio_status']} blocks={b['blocks']} "
              f"replica_sign_mean={b['replica_sign_mean']:.6g}+-{b['replica_sign_se']:.3g}")
        for key, (m, e) in b["observables"].items():
            print(f"  {key} {m:.12g} {e:.3g}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
