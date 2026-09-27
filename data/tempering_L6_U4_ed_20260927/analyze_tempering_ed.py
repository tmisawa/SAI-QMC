#!/usr/bin/env python3
"""Validate the dtau-ladder parallel-tempering (PT) implementation against
independent (non-PT) chains run at the same (beta, dtau), and compare both
against the finite-temperature grand-canonical ED reference for a periodic
L=6 Hubbard chain, U=4, mu=U/2 (half filling).

This is a correctness check of the PT machinery, not a scientific adoption
of any resulting numbers. See README.md in this directory for the full
write-up; this docstring only summarizes what the script does.

Steps
-----
1. Recompute every RNG seed the run should have used (PT per-slot chains,
   PT exchange/swap RNGs, and the four independent chains) from the
   `replica_seed` formula in src/replica.c, and check that all of them
   are pairwise distinct. Cross-check the formula against the seed values
   actually recorded in bins_pt.tsv, pt.tsv (ladder rows), and
   bins_fixed_b*.tsv, asserting the *expected counts* (64 PT chain seeds,
   16 PT swap seeds, 64 independent-chain seeds), not just printing
   whatever count happened to be found.
2. Parse the replica-bin files (validating each file's own `# columns:`
   and `# szz_Q_index=...` header lines against what this script assumes,
   and that `sum_sign == count`, i.e. the run really is sign-free), assert
   each independent-chain file's (beta_requested, Ltr) equals its PT
   slot's, then aggregate all bins belonging to one replica (ladder) into
   a single ratio-of-sums estimate per observable, and take the mean and
   standard error (SE) across the nrep=16 replicas. This is done
   independently for the PT run and for the four independent-chain runs,
   for four observables: E/N, D, M^2, Szz(Q). Every per-replica value,
   mean, SE, and z is required to be finite; a non-finite value aborts
   the script with a non-zero exit instead of silently passing through
   the screen below.
3. Form the PT-independent screen: for each of the 4 slots x 4
   observables (16 comparisons), z = (PT - indep) / sqrt(SE_PT^2 +
   SE_indep^2). Flag any |z| >= 3. The script exits non-zero if any
   comparison is flagged.
4. Report, for reference only (not pass/fail), the difference of the PT
   and independent-chain E/N and D against the finite-temperature ED
   values, together with the offset predicted by the existing L=6, U=4,
   mu=2 dtau^2-extrapolation slopes (data/L6_U4_mu2_dtau_ed_comparison/),
   interpolated in T. This states whether the observed ED difference is
   consistent with dtau^2 scaling (within the observed SE), not a proof
   of a Trotter-error origin.

Options
-------
--max-bin N restricts the aggregation in step 2/3 to bin_id < N (bins are
contiguous blocks of nmeas/nbin sweeps each), skips the SE-target and ED
sections, and does not overwrite the primary output files. This exists to
demonstrate that the earlier nmeas=20000 pass (bins 0..39 of these same
nmeas=50000, nbin=100 files, since both passes reused the same seeds) is
reproducible from the final data, not a separate run of independent value;
see README.md.
"""
from __future__ import annotations

import argparse
import csv
import math
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent
ED_REFERENCE = (
    ROOT.parent / "benchmark_L468_U4_full_diag_20260626" / "reference" / "L6_U4_FullDiag.dat"
)
DTAU2_SLOPE_CSV = (
    ROOT.parent / "L6_U4_mu2_dtau_ed_comparison"
    / "qmc_L6_U4_energy_doublon_dtau_extrap_vs_ED.csv"
)

N_SITE = 6
U = 4.0
MU = U / 2.0

PT_SEED = 270927001
INDEP_SEEDS = [270927101, 270927102, 270927103, 270927104]
BETAS = [4.0, 5.0, 6.666666666666667, 10.0]
TEMPS = [0.25, 0.2, 0.15, 0.1]
NREP = 16
NBETA = 4
LTR = 200

# Expected AF (staggered) and q=0 flat-q indices for this Lx=6 chain, as
# written by structure_factor_plan_init()'s "all" selector (q = mx, my=0):
# AF wavevector is mx=Lx/2=3, q=0 is mx=0.
AF_Q_INDEX = 3
Q0_INDEX = 0

E_OVER_N_SE_TARGET = 5.0e-4
D_SE_TARGET = 2.0e-4
Z_FLAG = 3.0

MASK64 = (1 << 64) - 1


# ---------------------------------------------------------------------------
# Step 1: replica_seed reimplementation (must match src/replica.c exactly)
# ---------------------------------------------------------------------------
def _splitmix64_value(x: int) -> int:
    x = (x + 0x9E3779B97F4A7C15) & MASK64
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & MASK64
    return (x ^ (x >> 31)) & MASK64


def replica_seed(base_seed: int, beta_index: int, replica_id: int) -> int:
    """Bit-exact port of replica_seed() in src/replica.c."""
    base_seed &= MASK64
    legacy = (base_seed + 1000 * beta_index) & MASK64
    if replica_id == 0:
        return legacy
    x = base_seed
    bi32 = (beta_index + 1) & 0xFFFFFFFF
    rid32 = (replica_id + 1) & 0xFFFFFFFF
    x ^= (0xD1B54A32D192ED03 * bi32) & MASK64
    x ^= (0xABC98388FB8FAC03 * rid32) & MASK64
    mixed = _splitmix64_value(x)
    if mixed == legacy:
        mixed = _splitmix64_value(mixed)
    return mixed


def enumerate_expected_seeds() -> list[tuple[str, int]]:
    """All seeds the run should use: PT per-slot chains, PT swap RNGs, and
    the four independent chains. 4*16 + 16 + 4*16 = 144 entries."""
    out = []
    for k in range(NBETA):
        for r in range(NREP):
            out.append((f"pt_slot{k}_r{r}", replica_seed(PT_SEED, k, r)))
    for r in range(NREP):
        out.append((f"pt_swap_r{r}", replica_seed(PT_SEED, NBETA, r)))
    for k, base in enumerate(INDEP_SEEDS):
        for r in range(NREP):
            out.append((f"indep_slot{k}_r{r}", replica_seed(base, 0, r)))
    return out


def check_seed_uniqueness() -> list[tuple[str, int]]:
    seeds = enumerate_expected_seeds()
    if len(seeds) != NBETA * NREP + NREP + NBETA * NREP or len(seeds) != 144:
        raise SystemExit(f"expected 144 enumerated seeds, got {len(seeds)}")
    by_value: dict[int, list[str]] = {}
    for label, val in seeds:
        by_value.setdefault(val, []).append(label)
    dups = {v: labels for v, labels in by_value.items() if len(labels) > 1}
    if dups:
        lines = ["DUPLICATE SEEDS DETECTED:"]
        for v, labels in dups.items():
            lines.append(f"  seed={v}: {labels}")
        raise SystemExit("\n".join(lines))
    return seeds


def cross_check_seeds_against_files() -> list[str]:
    """Cross-check the formula against seeds actually recorded on disk:
    bins_pt.tsv (per-slot chain seeds), pt.tsv ladder rows (swap seeds),
    and bins_fixed_b*.tsv (independent-chain seeds). Asserts the expected
    *count* found in each place (64, 16, and 64 respectively) rather than
    only reporting whatever count happened to be found -- an empty or
    truncated file would otherwise print a technically-true but useless
    "0 ... match" instead of failing."""
    msgs = []
    pt_bins = read_bin_file(ROOT / "bins_pt.tsv")
    seen = set()
    for row in pt_bins:
        key = (row["beta_index"], row["replica_id"])
        if key in seen:
            continue
        seen.add(key)
        expect = replica_seed(PT_SEED, row["beta_index"], row["replica_id"])
        if expect != row["seed"]:
            raise SystemExit(
                f"seed mismatch in bins_pt.tsv beta_index={row['beta_index']} "
                f"replica_id={row['replica_id']}: file={row['seed']} "
                f"formula={expect}"
            )
    if len(seen) != NBETA * NREP:
        raise SystemExit(
            f"bins_pt.tsv: expected {NBETA * NREP} distinct (beta_index,"
            f"replica_id) chain seeds, found {len(seen)}"
        )
    msgs.append(f"bins_pt.tsv: {len(seen)} (beta_index,replica_id) chain seeds match formula")

    nswap = 0
    for line in (ROOT / "pt.tsv").read_text().splitlines():
        if not line.startswith("ladder\t"):
            continue
        parts = line.split("\t")
        ladder = int(parts[1])
        swap_seed = int(parts[4])
        expect = replica_seed(PT_SEED, NBETA, ladder)
        if expect != swap_seed:
            raise SystemExit(
                f"swap seed mismatch in pt.tsv ladder={ladder}: "
                f"file={swap_seed} formula={expect}"
            )
        nswap += 1
    if nswap != NREP:
        raise SystemExit(f"pt.tsv: expected {NREP} ladder swap seeds, found {nswap}")
    msgs.append(f"pt.tsv: {nswap} ladder swap seeds match formula")

    total_indep = 0
    for k, base in enumerate(INDEP_SEEDS, start=1):
        bins = read_bin_file(ROOT / f"bins_fixed_b{k}.tsv")
        seen_r = set()
        for row in bins:
            if row["replica_id"] in seen_r:
                continue
            seen_r.add(row["replica_id"])
            expect = replica_seed(base, 0, row["replica_id"])
            if expect != row["seed"]:
                raise SystemExit(
                    f"seed mismatch in bins_fixed_b{k}.tsv replica_id="
                    f"{row['replica_id']}: file={row['seed']} formula={expect}"
                )
        if len(seen_r) != NREP:
            raise SystemExit(
                f"bins_fixed_b{k}.tsv: expected {NREP} replica seeds, found {len(seen_r)}"
            )
        total_indep += len(seen_r)
        msgs.append(f"bins_fixed_b{k}.tsv: {len(seen_r)} replica seeds match formula")
    if total_indep != NBETA * NREP:
        raise SystemExit(
            f"independent chains: expected {NBETA * NREP} total replica seeds "
            f"across {NBETA} files, found {total_indep}"
        )
    return msgs


# ---------------------------------------------------------------------------
# Step 2: replica-bin parsing and per-replica / per-slot aggregation
# ---------------------------------------------------------------------------
BIN_COLUMNS = [
    "beta_index", "beta_requested", "beta_effective", "Ltr", "replica_id",
    "seed", "bin_id", "sweep_begin", "sweep_end", "count", "sum_sign",
    "sum_sign_Ehub", "sum_sign_D", "local_accepted", "local_attempts",
    "global_accepted", "global_attempts", "sum_sign_Szz_Q",
    "sum_sign_Sperp_Q", "sum_sign_Szz_0",
]
INT_COLUMNS = {
    "beta_index", "Ltr", "replica_id", "seed", "bin_id", "sweep_begin",
    "sweep_end", "count", "local_accepted", "local_attempts",
    "global_accepted", "global_attempts",
}


def read_bin_file(path: Path) -> list[dict[str, float]]:
    """Parse a replica_bin_file (bins_pt.tsv / bins_fixed_b*.tsv).

    Validates this script's assumptions against the file's own header
    instead of only trusting the hardcoded BIN_COLUMNS order: the
    `# columns:` line must list exactly BIN_COLUMNS, and the
    `# szz_Q_index=... szz_0_index=... sperp_Q_index=...` line must match
    AF_Q_INDEX/Q0_INDEX/AF_Q_INDEX for this Lx=6 chain (if szz_q were not
    "all", these indices would be -1 and the corresponding sum_sign_Szz_*
    columns would be NaN -- exactly the case this guards against). Also
    asserts sum_sign == count for every row (the sign-free assumption this
    analysis relies on to treat the ratio of sums as a plain average).
    """
    text = path.read_text()
    columns_line = None
    index_line = None
    for line in text.splitlines():
        if line.startswith("# columns:"):
            columns_line = line
        elif line.startswith("# szz_Q_index="):
            index_line = line
        elif not line.startswith("#"):
            break
    if columns_line is None or index_line is None:
        raise SystemExit(f"{path}: missing '# columns:' or '# szz_Q_index=' header line")

    found_columns = columns_line[len("# columns:"):].strip().split("\t")
    if found_columns != BIN_COLUMNS:
        raise SystemExit(
            f"{path}: header column order changed.\n  expected: {BIN_COLUMNS}\n"
            f"  found:    {found_columns}"
        )

    index_vals = {}
    for token in index_line.lstrip("#").split():
        key, _, val = token.partition("=")
        if val:
            index_vals[key] = int(val)
    expected_indices = {
        "szz_Q_index": AF_Q_INDEX, "szz_0_index": Q0_INDEX, "sperp_Q_index": AF_Q_INDEX,
    }
    for key, expected in expected_indices.items():
        if index_vals.get(key) != expected:
            raise SystemExit(
                f"{path}: {key}={index_vals.get(key)!r}, expected {expected} for this "
                f"Lx=6 chain (a mismatch here means sum_sign_Szz_Q/sum_sign_Szz_0 are "
                f"the wrong q, or NaN -- e.g. from szz_q != all)"
            )

    rows = []
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) != len(BIN_COLUMNS):
            raise SystemExit(f"{path}: expected {len(BIN_COLUMNS)} columns, got {len(parts)}")
        row = {}
        for name, val in zip(BIN_COLUMNS, parts):
            row[name] = int(val) if name in INT_COLUMNS else float(val)
        if row["sum_sign"] != float(row["count"]):
            raise SystemExit(
                f"{path}: bin (beta_index={row['beta_index']}, replica_id="
                f"{row['replica_id']}, bin_id={row['bin_id']}): sum_sign="
                f"{row['sum_sign']!r} != count={row['count']!r} -- this run is not "
                f"sign-free, so the ratio-of-sums shortcut this analysis relies on "
                f"(README.md, 'Analysis method') does not apply as documented"
            )
        rows.append(row)
    return rows


def get_uniform_field(rows: list[dict[str, float]], beta_index: int, field: str):
    """The single value of `field` shared by every row with this
    beta_index (e.g. beta_requested, Ltr) -- fails loudly if it is not
    actually uniform, instead of silently taking the first match."""
    vals = {row[field] for row in rows if row["beta_index"] == beta_index}
    if len(vals) != 1:
        raise SystemExit(f"beta_index={beta_index}: expected a single {field}, got {vals}")
    return vals.pop()


def check_slot_metadata_matches(pt_rows, indep_rows: list[list[dict[str, float]]]) -> None:
    """Assert each independent-chain file's (beta_requested, Ltr) equals
    its corresponding PT slot's -- i.e. that slot k of the PT run and
    input_fixed_b{k+1}.in really describe the same physical point."""
    for k in range(NBETA):
        pt_beta = get_uniform_field(pt_rows, k, "beta_requested")
        pt_ltr = get_uniform_field(pt_rows, k, "Ltr")
        indep_beta = get_uniform_field(indep_rows[k], 0, "beta_requested")
        indep_ltr = get_uniform_field(indep_rows[k], 0, "Ltr")
        if pt_beta != indep_beta or pt_ltr != indep_ltr:
            raise SystemExit(
                f"slot {k}: PT (beta_requested={pt_beta}, Ltr={pt_ltr}) != "
                f"independent-chain file (beta_requested={indep_beta}, Ltr={indep_ltr})"
            )
        if pt_beta != BETAS[k] or pt_ltr != LTR:
            raise SystemExit(
                f"slot {k}: PT file says beta_requested={pt_beta}, Ltr={pt_ltr}, "
                f"expected beta={BETAS[k]}, Ltr={LTR}"
            )


@dataclass
class SlotStats:
    mean: float
    se: float
    per_replica: np.ndarray  # length NREP


def aggregate_per_replica(
    rows: list[dict[str, float]], beta_index: int, label: str, max_bin: int | None = None,
) -> dict[str, SlotStats]:
    """Sum all bins belonging to each replica (for the given beta_index),
    form the ratio-of-sums per replica, then reduce to mean+SE over
    replicas (SE = sample std, ddof=1, over sqrt(NREP)).

    `max_bin`, if given, restricts the sum to bin_id < max_bin (bins are
    contiguous blocks of nmeas/nbin sweeps each, in sweep order -- see
    README.md). Every per-replica ratio, and the resulting mean and SE,
    must be finite; a non-finite value raises SystemExit immediately
    instead of silently entering the PT-independent screen (e.g. as a
    NaN row that no comparison would ever flag).
    """
    by_rep: dict[int, dict[str, float]] = {}
    for row in rows:
        if row["beta_index"] != beta_index:
            continue
        if max_bin is not None and row["bin_id"] >= max_bin:
            continue
        rep = row["replica_id"]
        acc = by_rep.setdefault(
            rep, {"sum_sign": 0.0, "Ehub": 0.0, "D": 0.0, "SzzQ": 0.0, "Szz0": 0.0}
        )
        acc["sum_sign"] += row["sum_sign"]
        acc["Ehub"] += row["sum_sign_Ehub"]
        acc["D"] += row["sum_sign_D"]
        acc["SzzQ"] += row["sum_sign_Szz_Q"]
        acc["Szz0"] += row["sum_sign_Szz_0"]

    reps = sorted(by_rep)
    if reps != list(range(NREP)):
        raise SystemExit(f"{label}: expected replica_id 0..{NREP - 1}, got {reps}")

    def ratios(numerator_key: str, scale: float) -> np.ndarray:
        return np.array([scale * by_rep[r][numerator_key] / by_rep[r]["sum_sign"] for r in reps])

    raw = {
        "E/N": ratios("Ehub", 1.0 / N_SITE),
        "D": ratios("D", 1.0),
        "M2": ratios("Szz0", float(N_SITE)),
        "Szz(Q)": ratios("SzzQ", 1.0),
    }

    out: dict[str, SlotStats] = {}
    for obs_name, x in raw.items():
        bad = ~np.isfinite(x)
        if bad.any():
            raise SystemExit(
                f"{label}: non-finite per-replica value(s) for {obs_name} at replica(s) "
                f"{[reps[i] for i in np.nonzero(bad)[0]]}: {x[bad]}"
            )
        mean = float(np.mean(x))
        se = float(np.std(x, ddof=1) / math.sqrt(len(x)))
        if not (math.isfinite(mean) and math.isfinite(se)):
            raise SystemExit(f"{label}: non-finite mean/SE for {obs_name}: mean={mean} se={se}")
        out[obs_name] = SlotStats(mean=mean, se=se, per_replica=x)
    return out


# ---------------------------------------------------------------------------
# ED reference
# ---------------------------------------------------------------------------
def load_ed_reference() -> dict[float, dict[str, float]]:
    data = np.loadtxt(ED_REFERENCE)
    # columns: T, E_gc, C, N, Sz, S2, D_total, Z
    out = {}
    for row in data:
        T = row[0]
        out[round(T, 6)] = {
            "E_gc": row[1], "N": row[3], "D_total": row[6],
        }
    return out


def ed_lookup(T: float) -> dict[str, float]:
    table = load_ed_reference()
    key = round(T, 6)
    if key not in table:
        raise SystemExit(f"T={T} not found exactly on the ED grid (no interpolation done)")
    row = table[key]
    e_hub_total = row["E_gc"] + MU * row["N"]
    return {"E/N": e_hub_total / N_SITE, "D": row["D_total"] / N_SITE}


# ---------------------------------------------------------------------------
# dtau^2 reference: is the ED difference consistent with dtau^2 scaling?
# ---------------------------------------------------------------------------
def load_dtau2_slopes() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Per-T slopes d(E_total)/d(dtau^2) and d(D_per_site)/d(dtau^2) from the
    existing L=6, U=4, mu=2 dtau-extrapolation table (same system, same mu,
    different beta grid), sorted ascending in T for np.interp. E's slope is
    converted from total-energy to per-site (E/N) units by dividing by
    N_SITE; D's slope is already per-site in that table."""
    rows = list(csv.DictReader(DTAU2_SLOPE_CSV.open()))
    T = np.array([float(r["T"]) for r in rows])
    e_slope_total = np.array([float(r["E_slope_dtau2"]) for r in rows])
    d_slope = np.array([float(r["D_slope_dtau2"]) for r in rows])
    order = np.argsort(T)
    return T[order], e_slope_total[order] / N_SITE, d_slope[order]


def predicted_dtau2_offset(T: float, dtau: float) -> dict[str, float]:
    """Leading-order Trotter offset slope*dtau^2 predicted at temperature T,
    from linear interpolation (in T) of the L6/U4/mu2 dtau^2 slopes above.
    This is a reference check, not a proof: it uses a 3-point local slope
    fit from a different beta grid, interpolated (not measured) at this
    T, and does not include the slope's own fit uncertainty."""
    T_grid, e_slope_grid, d_slope_grid = load_dtau2_slopes()
    e_slope = float(np.interp(T, T_grid, e_slope_grid))
    d_slope = float(np.interp(T, T_grid, d_slope_grid))
    return {"E/N": e_slope * dtau ** 2, "D": d_slope * dtau ** 2}


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
def run_screen(pt_rows, indep_rows, max_bin: int | None) -> tuple[list[dict], float, int]:
    """Run the PT-vs-independent screen (step 2/3) and return
    (summary_rows, max_abs_z, n_flagged)."""
    observables = ["E/N", "D", "M2", "Szz(Q)"]
    obs_label = {"E/N": "E/N", "D": "D", "M2": "M^2", "Szz(Q)": "Szz(Q)"}

    summary_rows = []
    n_compared = 0
    n_flagged = 0
    max_abs_z = 0.0

    header = (
        f"{'slot':>4} {'beta':>10} {'T':>6} {'obs':>7} "
        f"{'PT_mean':>14} {'PT_SE':>11} {'Indep_mean':>14} {'Indep_SE':>11} "
        f"{'diff':>12} {'z':>8}"
    )
    print(header)
    print("-" * len(header))

    for k in range(NBETA):
        pt_stats = aggregate_per_replica(pt_rows, k, f"PT slot {k}", max_bin=max_bin)
        indep_stats = aggregate_per_replica(indep_rows[k], 0, f"independent slot {k}", max_bin=max_bin)
        for obs in observables:
            pt = pt_stats[obs]
            indep = indep_stats[obs]
            diff = pt.mean - indep.mean
            combined_se = math.sqrt(pt.se ** 2 + indep.se ** 2)
            if combined_se <= 0 or not math.isfinite(combined_se):
                raise SystemExit(
                    f"slot {k} {obs}: non-finite or non-positive combined_se={combined_se}"
                )
            z = diff / combined_se
            if not math.isfinite(z):
                raise SystemExit(f"slot {k} {obs}: non-finite z={z} (diff={diff}, combined_se={combined_se})")
            n_compared += 1
            flagged = abs(z) >= Z_FLAG
            if flagged:
                n_flagged += 1
            max_abs_z = max(max_abs_z, abs(z))
            print(
                f"{k:>4} {BETAS[k]:>10.6f} {TEMPS[k]:>6.3f} {obs_label[obs]:>7} "
                f"{pt.mean:>14.6e} {pt.se:>11.3e} {indep.mean:>14.6e} {indep.se:>11.3e} "
                f"{diff:>12.3e} {z:>8.2f}{'  <-- |z|>=3' if flagged else ''}"
            )
            summary_rows.append(
                {
                    "slot": k, "beta": BETAS[k], "T": TEMPS[k], "observable": obs_label[obs],
                    "pt_mean": pt.mean, "pt_se": pt.se,
                    "indep_mean": indep.mean, "indep_se": indep.se,
                    "diff": diff, "combined_se": combined_se, "z": z, "flagged": flagged,
                }
            )
    print()
    print(f"Comparisons: {n_compared} (expect {NBETA * len(observables)})")
    print(f"max |z| = {max_abs_z:.3f}; flagged (|z|>=3): {n_flagged}")
    print()
    return summary_rows, max_abs_z, n_flagged


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--max-bin", type=int, default=None,
        help=(
            "Restrict aggregation to bin_id < N (contiguous sweep blocks). "
            "Demonstrates that the earlier nmeas=20000 pass (N=40 here) is "
            "reproducible from the final nmeas=50000 files, not independent "
            "data. Skips the SE-target and ED sections and does not "
            "overwrite the primary output files."
        ),
    )
    args = parser.parse_args()

    print("=" * 78)
    print("Step 1: seed derivation and uniqueness")
    print("=" * 78)
    seeds = check_seed_uniqueness()
    print(f"Enumerated {len(seeds)} seeds from replica_seed(); all pairwise distinct.")
    for msg in cross_check_seeds_against_files():
        print("  " + msg)
    print()

    pt_rows = read_bin_file(ROOT / "bins_pt.tsv")
    indep_rows = [read_bin_file(ROOT / f"bins_fixed_b{k}.tsv") for k in range(1, NBETA + 1)]
    check_slot_metadata_matches(pt_rows, indep_rows)

    if args.max_bin is not None:
        print("=" * 78)
        print(f"Bin-limited reproduction: --max-bin {args.max_bin} "
              f"(bin_id < {args.max_bin} only; NOT the primary analysis)")
        print("=" * 78)
        _, max_abs_z, n_flagged = run_screen(pt_rows, indep_rows, max_bin=args.max_bin)
        print(f"VERDICT (bin-limited, informational only): max|z|={max_abs_z:.3f}; "
              f"flagged={n_flagged}")
        if n_flagged:
            raise SystemExit(f"FAIL (bin-limited): {n_flagged} comparison(s) with |z|>=3")
        return

    print("=" * 78)
    print("Step 2: per-replica aggregation, PT vs independent-chain screen")
    print("=" * 78)
    summary_rows, max_abs_z, n_flagged = run_screen(pt_rows, indep_rows, max_bin=None)

    print("=" * 78)
    print("Achieved SE vs precision targets (E/N SE <= 5e-4 t, D SE <= 2e-4)")
    print("=" * 78)
    se_header = f"{'slot':>4} {'beta':>10} {'run':>8} {'E/N SE':>12} {'ok':>4} {'D SE':>12} {'ok':>4}"
    print(se_header)
    print("-" * len(se_header))
    se_ok = True
    for k in range(NBETA):
        pt_stats = aggregate_per_replica(pt_rows, k, f"PT slot {k}")
        indep_stats = aggregate_per_replica(indep_rows[k], 0, f"independent slot {k}")
        for label, stats in (("PT", pt_stats), ("indep", indep_stats)):
            e_ok = stats["E/N"].se <= E_OVER_N_SE_TARGET
            d_ok = stats["D"].se <= D_SE_TARGET
            se_ok = se_ok and e_ok and d_ok
            print(
                f"{k:>4} {BETAS[k]:>10.6f} {label:>8} "
                f"{stats['E/N'].se:>12.3e} {'Y' if e_ok else 'N':>4} "
                f"{stats['D'].se:>12.3e} {'Y' if d_ok else 'N':>4}"
            )
    print()
    if se_ok:
        print("All achieved SEs meet the precision targets; nmeas increase not required.")
    else:
        print("Some achieved SEs exceed the precision targets (see 'N' above).")
    print()

    print("=" * 78)
    print("Reference only: PT and independent-chain E/N, D vs finite-T ED,")
    print("and vs the offset predicted by dtau^2 scaling (interpolated in T")
    print("from data/L6_U4_mu2_dtau_ed_comparison/...csv). Not a pass/fail")
    print("criterion, and 'consistent with dtau^2' below is not a proof.")
    print("=" * 78)
    ed_header = (
        f"{'slot':>4} {'beta':>10} {'T':>6} {'dtau':>10} {'obs':>5} "
        f"{'PT-ED':>12} {'PT_SE':>10} {'Indep-ED':>12} {'Indep_SE':>10} "
        f"{'pred_dtau2':>12} {'PT_dev/SE':>10} {'Ind_dev/SE':>10}"
    )
    print(ed_header)
    print("-" * len(ed_header))
    ed_rows = []
    for k in range(NBETA):
        dtau = BETAS[k] / LTR
        ed = ed_lookup(TEMPS[k])
        predicted = predicted_dtau2_offset(TEMPS[k], dtau)
        pt_stats = aggregate_per_replica(pt_rows, k, f"PT slot {k}")
        indep_stats = aggregate_per_replica(indep_rows[k], 0, f"independent slot {k}")
        for obs in ("E/N", "D"):
            pt_diff = pt_stats[obs].mean - ed[obs]
            indep_diff = indep_stats[obs].mean - ed[obs]
            pred = predicted[obs]
            pt_dev_over_se = (pt_diff - pred) / pt_stats[obs].se
            indep_dev_over_se = (indep_diff - pred) / indep_stats[obs].se
            print(
                f"{k:>4} {BETAS[k]:>10.6f} {TEMPS[k]:>6.3f} {dtau:>10.6f} {obs:>5} "
                f"{pt_diff:>12.3e} {pt_stats[obs].se:>10.3e} "
                f"{indep_diff:>12.3e} {indep_stats[obs].se:>10.3e} "
                f"{pred:>12.3e} {pt_dev_over_se:>10.2f} {indep_dev_over_se:>10.2f}"
            )
            ed_rows.append(
                {
                    "slot": k, "beta": BETAS[k], "T": TEMPS[k], "dtau": dtau,
                    "observable": obs, "ed_value": ed[obs],
                    "pt_minus_ed": pt_diff, "pt_se": pt_stats[obs].se,
                    "indep_minus_ed": indep_diff, "indep_se": indep_stats[obs].se,
                    "predicted_dtau2_offset": pred,
                    "pt_dev_from_predicted_over_se": pt_dev_over_se,
                    "indep_dev_from_predicted_over_se": indep_dev_over_se,
                }
            )
    print()
    n_consistent = sum(
        1 for r in ed_rows
        if abs(r["pt_dev_from_predicted_over_se"]) < Z_FLAG
        and abs(r["indep_dev_from_predicted_over_se"]) < Z_FLAG
    )
    print(
        f"{n_consistent}/{len(ed_rows)} (slot, observable) rows have both PT and "
        f"independent ED-differences within {Z_FLAG:.0f} SE of the dtau^2-predicted "
        "offset (predicted_dtau2_offset column; slope interpolated in T from the "
        "L6/U4/mu2 dtau-extrapolation table, a different beta grid). This is "
        "'consistent with dtau^2 dependence' in that limited sense -- it is not a "
        "proof, does not include the slope's own fit uncertainty, and each slot "
        "here also has a different T, which the interpolation only approximately "
        "corrects for."
    )
    print()

    # Write machine-readable summaries.
    with (ROOT / "pt_vs_independent_screen.tsv").open("w", newline="") as f:
        fieldnames = list(summary_rows[0].keys())
        w = csv.DictWriter(f, fieldnames=fieldnames, delimiter="\t", lineterminator="\n")
        w.writeheader()
        w.writerows(summary_rows)

    with (ROOT / "pt_independent_vs_ed.tsv").open("w", newline="") as f:
        fieldnames = list(ed_rows[0].keys())
        w = csv.DictWriter(f, fieldnames=fieldnames, delimiter="\t", lineterminator="\n")
        w.writeheader()
        w.writerows(ed_rows)

    print("=" * 78)
    verdict_pass = n_flagged == 0
    print(f"VERDICT: max|z|={max_abs_z:.3f} over {len(summary_rows)} comparisons; "
          f"{'PASS (no |z|>=3)' if verdict_pass else 'FAIL: ' + str(n_flagged) + ' comparison(s) with |z|>=3'}")
    print(
        "This PASS means the PT and independent-chain results are statistically "
        "consistent within their SEs for every comparison tested; it does not by "
        "itself show equal precision between PT and independent chains, and it is "
        "not a general proof of correctness beyond the cases and statistics "
        "exercised here."
    )
    print("=" * 78)
    if not verdict_pass:
        raise SystemExit(f"FAIL: {n_flagged} comparison(s) with |z| >= {Z_FLAG}")


if __name__ == "__main__":
    main()
