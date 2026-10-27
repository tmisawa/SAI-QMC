"""Record selected tool versions; never dump credentials or the environment."""
import json
import os
import platform
import subprocess
import sys


def capture(command):
    return subprocess.check_output(command, text=True, stderr=subprocess.STDOUT).strip()


def main():
    compiler = os.environ.get("CC", "cc")
    record = {"os": platform.system(), "arch": platform.machine(), "logical_cpus": os.cpu_count(),
              "python": sys.version, "child_python": capture(["python3", "--version"]),
              "compiler": capture([compiler, "--version"]),
              "make": capture(["make", "--version"]).splitlines()[0],
              "blas_threads": {key: os.environ[key] for key in
                               ("OPENBLAS_NUM_THREADS", "VECLIB_MAXIMUM_THREADS",
                                "MKL_NUM_THREADS", "BLIS_NUM_THREADS")},
              "openmp_dynamic": os.environ["OMP_DYNAMIC"]}
    profile = os.environ["CI_PROFILE"]
    expected_os = "Darwin" if profile == "macos" else "Linux"
    if record["os"] != expected_os:
        raise ValueError(f"{profile} requires {expected_os}")
    if profile.startswith("linux-"):
        expected_major = "18" if profile == "linux-clang" else "13"
        if capture([compiler, "-dumpversion"]).split(".")[0] != expected_major:
            raise ValueError("Unexpected compiler major version")
    if profile == "linux-parallel":
        record["mpi_compiler"] = capture([os.environ.get("MPICC", "mpicc"), "--showme:command"])
        record["mpi_version"] = capture([os.environ.get("CI_MPIEXEC", "mpirun"), "--version"])
        if record["mpi_compiler"] != compiler:
            raise ValueError("MPI wrapper does not use the requested C compiler")
        if "Open MPI" not in record["mpi_version"] and "OpenRTE" not in record["mpi_version"]:
            raise ValueError("Expected Open MPI")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
