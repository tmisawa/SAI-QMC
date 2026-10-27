"""Verify frozen artifacts and reanalyse independent copies, without QMC or ED runs."""
import argparse
import csv
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile

from common import run, sha256, write_json

ROOT = Path(__file__).resolve().parents[1]
APP2 = "antiperiodic_4x2_U4_ed"
APP4 = "antiperiodic_4x4_U4_ed"
PP4 = "periodic_4x4_U4_control"
TABLES = ("group_estimates.tsv", "dtau_extrapolated.tsv", "fine_two_point.tsv",
          "temperature_comparison.tsv", "spin_su2_all_q.tsv", "half_run_drift.tsv",
          "seed_estimates.tsv")
# Identifiers must never pass a floating-point tolerance (notably 64-bit seeds).
EXACT_COLUMNS = {"name", "beta", "dtau", "series", "seed", "input_sha256", "observable",
                 "dof", "qx", "qy", "mx", "my", "boundary", "method", "error_method", "block_size", "block_sweeps",
                 "nbin", "nseed", "beta_low", "beta_high"}


def compare_json(expected, actual, where="root"):
    if type(expected) is not type(actual):
        raise ValueError(f"Type changed: {where}")
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            raise ValueError(f"Keys changed: {where}")
        for key in expected:
            compare_json(expected[key], actual[key], f"{where}.{key}")
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            raise ValueError(f"Length changed: {where}")
        for i, (left, right) in enumerate(zip(expected, actual)):
            compare_json(left, right, f"{where}[{i}]")
    elif isinstance(expected, float):
        if not (math.isfinite(expected) and math.isfinite(actual)
                and math.isclose(expected, actual, rel_tol=1e-10, abs_tol=1e-12)):
            raise ValueError(f"Numeric mismatch: {where}: {expected} != {actual}")
    elif expected != actual:
        raise ValueError(f"Value changed: {where}: {expected!r} != {actual!r}")


def compare_tsv(expected, actual):
    def read(path):
        with Path(path).open(newline="") as stream:
            return list(csv.reader(stream, delimiter="\t"))
    left, right = read(expected), read(actual)
    if not left or len(left) != len(right) or left[0] != right[0]:
        raise ValueError(f"Table schema/rows changed: {expected.name}")
    header = left[0]
    for index, (a, b) in enumerate(zip(left[1:], right[1:]), 1):
        if len(a) != len(header) or len(b) != len(header):
            raise ValueError(f"Malformed row: {expected.name}:{index}")
        for key, x, y in zip(header, a, b):
            where = f"{expected.name}:{index}:{key}"
            if key in EXACT_COLUMNS:
                compare_json(x, y, where)
            else:
                try:
                    xx, yy = float(x), float(y)
                except ValueError:
                    compare_json(x, y, where)
                else:
                    compare_json(xx, yy, where)


def verify_manifest(folder, filename, expected_count, base=None):
    base = folder if base is None else base
    names = set()
    for line in (folder / filename).read_text().splitlines():
        digest, name = line.split("  ", 1)
        path = (base / name).resolve()
        if not path.is_relative_to(base.resolve()) or name in names:
            raise ValueError(f"Invalid manifest entry: {name}")
        names.add(name)
        if sha256(path) != digest:
            raise ValueError(f"Checksum mismatch: {name}")
    if len(names) != expected_count:
        raise ValueError(f"Manifest count changed: {filename}: {len(names)}")
    return len(names)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "ci-results/saved-data")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "verification.json").unlink(missing_ok=True)
    counts = {"historical": verify_manifest(ROOT / "data", "SHA256SUMS", 1174, ROOT)}
    counts[APP2] = verify_manifest(ROOT / "data" / APP2, "preregistration.sha256", 12)
    counts[APP4] = verify_manifest(ROOT / "data" / APP4, "SHA256SUMS", 382)
    counts[PP4] = verify_manifest(ROOT / "data" / PP4, "SHA256SUMS", 390)
    run([sys.executable, "scripts/verify_reference_data.py"], ROOT, output / "historical")
    comparisons = []
    with tempfile.TemporaryDirectory(prefix="sai-reanalysis-") as scratch:
        scratch = Path(scratch)
        for label, dataset, script, files, commands in [
            ("app2", APP2, "analyze_antiperiodic_ed.py", ["summary.json", "analysis_output.md"],
             [["analyze_antiperiodic_ed.py", "--self-test"], ["test_validation.py"]]),
            ("app4", APP4, "analyze.py", ["analysis.json", *TABLES], []),
            ("pp4", PP4, "analyze.py", ["analysis.json", *TABLES], []),
            ("paired", PP4, "compare_boundaries.py",
             ["control_comparison.json", "boundary_differences.tsv", "paired_residual_extrapolated.tsv",
              "error_method_comparison.tsv"], []),
        ]:
            # Paired analysis sees only frozen sibling datasets, never the output
            # of a previous analysis under a different Python or math library.
            parent = scratch / label
            folder = parent / dataset
            shutil.copytree(ROOT / "data" / dataset, folder)
            if label == "paired":
                shutil.copytree(ROOT / "data" / APP4, parent / APP4)
            for i, command in enumerate(commands):
                run([sys.executable, "-B", *command], folder, output / label / f"check-{i}")
            # Missing regenerated output must not be masked by the shipped file.
            for name in files:
                (folder / name).unlink()
            run([sys.executable, "-B", script], folder, output / label / "analysis", timeout=300)
            for name in files:
                expected, actual = ROOT / "data" / dataset / name, folder / name
                target = output / label / name
                shutil.copyfile(actual, target)
                if name.endswith(".json"):
                    compare_json(json.loads(expected.read_text()), json.loads(actual.read_text()), name)
                elif name.endswith(".tsv"):
                    compare_tsv(expected, actual)
                else:
                    # Human-readable report has rounded values; JSON above is
                    # the full-precision acceptance result.
                    if expected.read_bytes() != actual.read_bytes():
                        raise ValueError(f"Rounded report changed: {name}")
                comparisons.append({"analysis": label, "file": name,
                                    "byte_identical": sha256(expected) == sha256(actual)})
    write_json(output / "verification.json", {"status": "pass", "checksums": counts,
                                              "comparisons": comparisons})
    print(f"PASS: {sum(counts.values())} checksums, {len(comparisons)} regenerated files")


if __name__ == "__main__":
    main()
