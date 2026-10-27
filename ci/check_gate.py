"""Reject failed, cancelled, skipped, missing or incomplete matrix results."""
import argparse
import json
from pathlib import Path

from config import CONFIGS, case_id, parallel_cases


def validate(folder, matrix_status, source_sha, head_sha):
    if matrix_status != "success":
        raise ValueError(f"Matrix did not succeed: {matrix_status}")
    results = list(Path(folder).glob("*/result.json"))
    if len(results) != len(CONFIGS):
        raise ValueError("Missing or duplicate configuration results")
    seen = set()
    for path in results:
        result = json.loads(path.read_text())
        config = result["config"]
        if config not in CONFIGS or config in seen:
            raise ValueError(f"Unexpected or duplicate configuration: {config}")
        seen.add(config)
        if result["status"] != "pass" or result["source_sha"] != source_sha or result["pr_head_sha"] != head_sha:
            raise ValueError(f"Failed or stale result: {config}")
        expected = [{"id": stage, "status": "pass"} for stage in CONFIGS[config]]
        if result["stages"] != expected:
            raise ValueError(f"Incomplete stages: {config}")
        expected_cases = [case_id(case) for case in parallel_cases(config)] if config in ("linux-parallel", "macos") else []
        if result["parallel_cases"] != expected_cases:
            raise ValueError(f"Incomplete parallel cases: {config}")
    if seen != set(CONFIGS):
        raise ValueError("Required configurations are missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("folder", type=Path)
    parser.add_argument("--matrix-status", required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--head-sha", required=True)
    args = parser.parse_args()
    validate(args.folder, args.matrix_status, args.source_sha, args.head_sha)
    print("PASS: all four required configurations, stages and parallel cases")


if __name__ == "__main__":
    main()
