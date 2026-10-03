"""Subprocess time limits and machine-readable evidence shared by CI helpers."""
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import time


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def run(command, cwd, output, timeout=180, env=None):
    """Keep logs on failure; kill the whole local process group on timeout."""
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    command = list(map(str, command))
    start = time.monotonic()
    result = {"command": command, "timeout_seconds": timeout, "timed_out": False,
              "exit_code": None}
    try:
        with (output / "stdout.txt").open("w") as out, (output / "stderr.txt").open("w") as err:
            process = subprocess.Popen(command, cwd=cwd, env=env, stdout=out, stderr=err,
                                       start_new_session=True)
            def stop(signum, frame):
                # Let nested CI helpers terminate their own process groups too.
                raise InterruptedError(f"Received signal {signum}")

            handlers = {sig: signal.signal(sig, stop) for sig in (signal.SIGINT, signal.SIGTERM)}
            try:
                result["exit_code"] = process.wait(timeout=timeout)
            except (subprocess.TimeoutExpired, InterruptedError) as exc:
                result["timed_out"] = isinstance(exc, subprocess.TimeoutExpired)
                result["interrupted"] = isinstance(exc, InterruptedError)
                os.killpg(process.pid, signal.SIGTERM)
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()
                # A child can ignore TERM even after the direct parent exits.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                result["exit_code"] = process.returncode
            finally:
                for sig, handler in handlers.items():
                    signal.signal(sig, handler)
    except OSError as exc:
        result["error"] = str(exc)
    finally:
        result["elapsed_seconds"] = time.monotonic() - start
        write_json(output / "command.json", result)
    if result["timed_out"] or result.get("interrupted") or result["exit_code"] != 0:
        raise RuntimeError(f"Command failed (exit={result['exit_code']}, "
                           f"timeout={result['timed_out']}): {command}")
    return result
