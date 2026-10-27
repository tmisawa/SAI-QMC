"""Negative controls for the CI gate, comparisons, launcher and timeout handling."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from check_gate import validate
from common import run, write_json
from config import CONFIGS, case_id, parallel_cases
from verify_validation_data import compare_json, compare_tsv, verify_manifest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("parallel_test", ROOT / "tests/test_parallel_configurations.py")
parallel = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parallel)


class Helpers(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)

    def gate_fixtures(self):
        for config, stages in CONFIGS.items():
            folder = self.root / config
            folder.mkdir()
            write_json(folder / "result.json", {
                "config": config, "source_sha": "source", "pr_head_sha": "head", "status": "pass",
                "stages": [{"id": stage, "status": "pass"} for stage in stages],
                "parallel_cases": [case_id(case) for case in parallel_cases(config)]
                if config in ("linux-parallel", "macos") else [],
            })

    def test_gate_rejects_failure_cancel_skip(self):
        self.gate_fixtures()
        validate(self.root, "success", "source", "head")
        for state in ("failure", "cancelled", "skipped"):
            with self.subTest(state=state), self.assertRaises(ValueError):
                validate(self.root, state, "source", "head")

    def test_gate_rejects_missing_job(self):
        self.gate_fixtures()
        (self.root / "linux-clang/result.json").unlink()
        with self.assertRaises(ValueError):
            validate(self.root, "success", "source", "head")

    def test_gate_rejects_failed_stale_or_incomplete_result(self):
        self.gate_fixtures()
        path = self.root / "linux-parallel/result.json"
        original = json.loads(path.read_text())
        variants = []
        for key, value in [("status", "failure"), ("source_sha", "old"), ("pr_head_sha", "old"),
                           ("stages", original["stages"][:-1]), ("parallel_cases", original["parallel_cases"][:-1]),
                           ("config", "linux-gcc")]:
            altered = copy.deepcopy(original)
            altered[key] = value
            variants.append(altered)
        altered = copy.deepcopy(original)
        altered["stages"][0]["status"] = "failure"
        variants.append(altered)
        for altered in variants:
            write_json(path, altered)
            with self.subTest(altered=altered), self.assertRaises(ValueError):
                validate(self.root, "success", "source", "head")

    def test_numeric_schema_and_verdict(self):
        compare_json({"value": 1.0, "seed": 2**63}, {"value": 1.0 + 1e-12, "seed": 2**63})
        for a, b in [({"x": 1.0}, {"x": 1.1}), ({"x": 1.0}, {}),
                     ({"verdict": "PASS"}, {"verdict": "FAIL"}),
                     ({"seed": 2**63}, {"seed": 2**63 + 1}),
                     ([1.0], [float("nan")]), ([1.0], [float("inf")]), ([1.0], [])]:
            with self.subTest(a=a, b=b), self.assertRaises(ValueError):
                compare_json(a, b)

    def test_table_seed_schema_and_nonfinite(self):
        left, right = self.root / "expected.tsv", self.root / "actual.tsv"
        left.write_text("seed\tmean\n9223372036854775808\t1.0\n")
        for text in ["seed\tmean\n9223372036854775809\t1.0\n",
                     "seed\tmean\n9223372036854775808\tnan\n",
                     "seed\n9223372036854775808\n", "seed\tmean\n"]:
            right.write_text(text)
            with self.subTest(text=text), self.assertRaises(ValueError):
                compare_tsv(left, right)

    def test_manifest_rejects_mutation(self):
        (self.root / "raw").write_text("a")
        (self.root / "SHA256SUMS").write_text("0" * 64 + "  raw\n")
        with self.assertRaises(ValueError):
            verify_manifest(self.root, "SHA256SUMS", 1)

    def test_parallel_missing_bin_and_changed_value(self):
        left, right = self.root / "left", self.root / "right"
        left.write_text("# columns: replica bin value\n0 0 1.5\n0 1 2.5\n")
        for text in ["# columns: replica bin value\n0 0 1.5\n",
                     "# columns: replica bin value\n0 0 1.6\n0 1 2.5\n",
                     "# columns: replica bin value\n0 0 nan\n0 1 2.5\n"]:
            right.write_text(text)
            with self.subTest(text=text), self.assertRaises(ValueError):
                parallel.compare_files(left, right, self.root / "diff")

    def test_timeout_kills_process_group(self):
        script = "import subprocess,sys,time; p=subprocess.Popen([sys.executable,'-c','import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)']); open('child.pid','w').write(str(p.pid)); time.sleep(60)"
        with self.assertRaises(RuntimeError):
            run([sys.executable, "-c", script], self.root, self.root / "logs", timeout=1)
        result = json.loads((self.root / "logs/command.json").read_text())
        self.assertTrue(result["timed_out"])
        pid = (self.root / "child.pid").read_text()
        state = subprocess.run(["ps", "-o", "stat=", "-p", pid], capture_output=True, text=True).stdout.strip()
        self.assertTrue(not state or state.startswith("Z"), state)

    def test_failed_shell_test_preserves_evidence_and_exit(self):
        for status in (0, 7):
            work = self.root / f"work-{status}"
            work.mkdir()
            (work / "evidence").write_text("retained failure")
            saved = self.root / f"saved-{status}"
            script = '''. "$1"; trap 'test_cleanup "$2"' EXIT; exit "$3"'''
            result = subprocess.run(["sh", "-c", script, "fixture.sh", str(ROOT / "tests/test_cleanup.sh"),
                                     str(work), str(status)], env=dict(os.environ, CI_FAILURE_DIR=str(saved)))
            self.assertEqual(result.returncode, status)
            self.assertFalse(work.exists())
            copies = list(saved.glob("fixture.sh.*"))
            self.assertEqual(len(copies), int(status != 0))
            if copies:
                self.assertEqual((copies[0] / "evidence").read_text(), "retained failure")

    def test_repeated_shell_failures_keep_separate_evidence(self):
        saved = self.root / "saved failures"
        work = self.root / "same work directory"
        statuses = (7, 9, 11)
        for status in statuses:
            work.mkdir()
            (work / "evidence").write_text(str(status))
            (work / ".hidden").write_text(str(status))
            script = '''. "$1"; trap 'test_cleanup "$2"' EXIT; exit "$3"'''
            result = subprocess.run(["sh", "-eu", "-c", script, "fixture.sh",
                                     str(ROOT / "tests/test_cleanup.sh"), str(work), str(status)],
                                    env=dict(os.environ, CI_FAILURE_DIR=str(saved)))
            self.assertEqual(result.returncode, status)
            self.assertFalse(work.exists())
        copies = list(saved.glob("fixture.sh.*"))
        self.assertEqual(len(copies), len(statuses))
        self.assertEqual({(copy / "evidence").read_text() for copy in copies},
                         {str(status) for status in statuses})
        for copy in copies:
            self.assertEqual({path.name for path in copy.iterdir()}, {"evidence", ".hidden"})
            self.assertEqual((copy / ".hidden").read_text(), (copy / "evidence").read_text())

    def test_failed_evidence_archive_keeps_original_and_exit(self):
        work = self.root / "work"
        work.mkdir()
        (work / "evidence").write_text("retained failure")
        saved = self.root / "not a directory"
        saved.write_text("existing file")
        script = '''. "$1"; trap 'test_cleanup "$2"' EXIT; exit 7'''
        result = subprocess.run(["sh", "-eu", "-c", script, "fixture.sh",
                                 str(ROOT / "tests/test_cleanup.sh"), str(work)],
                                env=dict(os.environ, CI_FAILURE_DIR=str(saved)), capture_output=True)
        self.assertEqual(result.returncode, 7)
        self.assertEqual((work / "evidence").read_text(), "retained failure")
        self.assertEqual(saved.read_text(), "existing file")

    def test_launcher_preserves_arguments_and_exit(self):
        launcher = self.root / "launcher with spaces"
        launcher.write_text("#!/bin/sh\nif [ \"$1\" = --version ]; then echo 'Open MPI test'; exit 0; fi\nprintf '%s\\n' \"$@\" > \"$FAKE_ARGS\"\nexit 7\n")
        launcher.chmod(0o755)
        record = self.root / "args"
        result = subprocess.run(["sh", str(ROOT / "ci/mpi-launcher.sh"), "-n", "2", "two words"],
                                env=dict(os.environ, CI_MPIEXEC=str(launcher), FAKE_ARGS=str(record)))
        self.assertEqual(result.returncode, 7)
        self.assertEqual(record.read_text().splitlines(),
                         ["--oversubscribe", "--bind-to", "none", "-n", "2", "two words"])

    def test_launcher_rejects_missing_or_wrong_implementation(self):
        launcher = self.root / "fake-launcher"
        launcher.write_text('#!/bin/sh\necho "MPICH Version: test"\n')
        launcher.chmod(0o755)
        for candidate in [launcher, self.root / "missing"]:
            result = subprocess.run(["sh", str(ROOT / "ci/mpi-launcher.sh"), "-n", "1", "true"],
                                    env=dict(os.environ, CI_MPIEXEC=str(candidate)), capture_output=True)
            self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
