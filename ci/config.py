"""The four required PR configurations and their expected stages."""
CONFIGS = {
    "linux-gcc": ["environment", "ci-regressions", "build-serial", "test-serial",
                  "legacy-tempering", "legacy-boundary", "saved-data"],
    "linux-clang": ["environment", "ci-regressions", "build-serial", "test-serial"],
    "linux-parallel": ["environment", "ci-regressions", "build-parallel", "test-omp",
                       "test-mpi", "test-hybrid", "parallel-configurations"],
    "macos": ["environment", "ci-regressions", "build-macos", "test-serial",
              "test-omp", "parallel-configurations"],
}


def parallel_cases(profile):
    """Every tuple is (feature, replicas, mode, ranks, threads)."""
    modes = [("omp", 1, 1), ("omp", 1, 2)]
    if profile == "linux-parallel":
        modes += [("omp", 1, 4), ("mpi", 1, 1), ("mpi", 2, 1), ("mpi", 4, 1),
                  ("hybrid", 1, 2), ("hybrid", 2, 1), ("hybrid", 2, 2)]
    elif profile != "macos":
        raise ValueError(f"Not a parallel profile: {profile}")
    cases = []
    for feature in ("app", "global", "pt", "conditional"):
        cases += [(feature, 5, *mode) for mode in modes]
        cases.append((feature, 1, "omp", 1, 4 if profile == "linux-parallel" else 2))
        if profile == "linux-parallel":
            cases.append((feature, 2, "mpi", 4, 1))
    return cases


def case_id(case):
    feature, replicas, mode, ranks, threads = case
    return f"{feature}-n{replicas}-{mode}-r{ranks}-t{threads}"
