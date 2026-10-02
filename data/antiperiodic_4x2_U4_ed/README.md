---
date: 2026-10-02
datetime: 2026-10-02 22:25 JST
model: OpenAI GPT-5.6 Sol (Codex)
summary: |
  Pre-registered validation of built-in antiperiodic boundaries (bc_x/bc_y): 4x2 square
  lattice, x antiperiodic / y periodic, U=4, beta=2 and 4. 96 independent single-series DQMC
  runs at dtau=0.1/0.05/0.025, extrapolated in dtau^2 and compared with finite-temperature ED.
  Verdict: PASS.
---

# Antiperiodic boundary validation: 4x2, U=4, versus finite-temperature ED

This is the pre-registered check of the built-in antiperiodic boundary
(`bc_x=antiperiodic`), including the spin structure factors. The criteria were fixed
before the runs: `preregistration.sha256` lists the scripts, seeds, reference matrices and
ED results committed before any production run (commit `b3fee373d587ddd689aa61f126f9f35c3ac78954`).

## Conditions

- Lattice `lattice=square Lx=4 Ly=2 bc_x=antiperiodic bc_y=periodic`, `t=-1`, `U=4`, `mu=U/2`.
  The length-2 periodic y direction carries the doubled bond (amplitude `2t`).
- beta = 2, 4; dtau = 0.1, 0.05, 0.025.
- 16 independent series per (dtau, beta), each one run with `nrep=1`, a single beta and the
  chain seed from `seeds.tsv` (`replica_seed(base, beta_index, series)`, bases 2026100201,
  2026100202, 2026100203 for dtau 0.1, 0.05, 0.025). `smoke/` checks these seeds against the
  program.
- `nwarm=2000 nmeas=40000 nbin=20 stab=4`, `szz_q=all sperp_q=all`, `replica_bin_file=bins.tsv`,
  with the defaults written out (`tempering=none global_update=none conditional_measure=0
  field_init=random sweep_order=forward green_rebuild=combine`). Every input is in its run directory.
- Serial binary, at most 4 processes at a time, BLAS on one thread, nice 10.

## Reference

- `ed_finite_t.py`: grand-canonical finite-temperature ED over all (N_up, N_down) sectors of
  `hopping_app.txt` (AP/P) and `hopping_pp.txt` (P/P), both written by this program
  (`matrix_app/`, `matrix_pp/`). Results: `ed_app.json`, `ed_pp.json`.
- `check_ed_reference.py`: at U=0 the ED equals momentum formulas with half-integer kx;
  both references are half filled and obey the sum rule.

## Criteria (fixed before the runs)

Each observable is averaged over the 16 series; s_i is the standard error of that mean.
a and sigma_a come from a weighted least-squares line in dtau^2 with absolute errors
(no rescaling by the residual chi^2); z = (a - ED) / sigma_a.

- P1: E/N, D, Szz(Q), Sperp(Q)/2 versus the AP/P ED, |z| < 3.5 (8 comparisons).
- P2: Szz(q), Sperp(q)/2 for all 8 q versus the AP/P ED, |z| < 4 (32 comparisons).
- N1 (negative control): D, Szz(1,1), Szz(1,0) at beta=4 versus the P/P ED, |z| > 5.
- HOLD if any fit has p < 0.001, non-finite input/reference/result, non-positive error or a
  singular normal matrix, or if either spin channel disagrees with its bin sum at Q
  (relative tolerance 1e-12); never drop points or rerun numerical HOLD series.
- INVALID for missing/malformed artifacts, duplicate bin/q indices, mismatched run conditions
  or checksum/provenance failures. Both report files are replaced on every analysis outcome.

## Reproduce

```sh
make dqmc                                   # repository root
cd data/antiperiodic_4x2_U4_ed
python3 analyze_antiperiodic_ed.py --self-test
python3 -B test_validation.py
python3 run_validation.py seeds && python3 run_validation.py smoke
python3 run_validation.py matrices
python3 ed_finite_t.py hopping_app.txt 4 2 4 2,4 > ed_app.json
python3 ed_finite_t.py hopping_pp.txt 4 2 4 2,4 > ed_pp.json
python3 check_ed_reference.py
python3 run_validation.py inputs && python3 run_validation.py run
python3 analyze_antiperiodic_ed.py
```

Re-analysing the shipped outputs needs only the last command (standard library only). The
per-run matrix check uses `runs/.../hopping_used.sha256`, because `hopping_used.txt` is not tracked.
Each run also carries `started.json` and `complete.json`, binding its input, binary and required
outputs by SHA-256. A resume verifies these records before skipping a run. Interrupted complete
outputs can recover their marker; incomplete or structurally invalid attempts are preserved under
`attempts/` before retrying the same input and seed. A missing matrix digest is recovered only from
the matching actual matrix file. Numerical HOLD outcomes are preserved without rerunning them.

## Provenance

- Source: SAI-QMC `feat/directional-bc`, commit `b3fee373d587ddd689aa61f126f9f35c3ac78954`.
- Binary `dqmc` SHA-256 `d685f060cc9e1f88547868b5fabeeda0245c284c5a218923d6d6e3d52ed5ce4f`
  (also in `run_log.jsonl`); Apple clang version 16.0.0 (clang-1600.0.26.6).
- Python 3.10.5, numpy 2.2.6 (ED, ED checks and the self-test only).
- Production run time: 301.0 s with 4 workers.
- Resume history: the initial production command completed all 96 runs with 0 failures,
  0 recovered runs, 0 verified skips, and no archived attempts.

## Result

# 4x2 AP/P U=4 validation: analysis

96 runs (dtau 0.1, 0.05, 0.025 x beta 2, 4 x 16 independent series), all with the
pre-registered inputs. a and sigma_a: weighted least squares in dtau^2, absolute errors.

| beta | observable | role | a | sigma_a | ED AP/P | z AP/P | ED P/P | z P/P | chi2 | p |
| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 2 | E/N | P1 | -1.124857 | 0.000307 | -1.124787 | -0.23 | -1.171671 | 152.68 | 0.934 | 0.334 |
| 2 | D | P1 | 0.142159 | 0.000036 | 0.142156 | 0.09 | 0.150787 | -237.01 | 2.989 | 0.0838 |
| 2 | Szz(Q) | P1 | 0.354034 | 0.000321 | 0.354115 | -0.25 | 0.359740 | -17.78 | 4.938 | 0.0263 |
| 2 | Sperp(Q)/2 | P1 | 0.354451 | 0.000291 | 0.354115 | 1.15 | 0.359740 | -18.15 | 0.011 | 0.916 |
| 2 | Szz(0,0) | P2 | 0.062267 | 0.000114 | 0.062320 | -0.46 | 0.050004 | 107.74 | 2.269 | 0.132 |
| 2 | Sperp(0,0)/2 | P2 | 0.062606 | 0.000228 | 0.062320 | 1.25 | 0.050004 | 55.16 | 3.232 | 0.0722 |
| 2 | Szz(1,0) | P2 | 0.062473 | 0.000074 | 0.062506 | -0.45 | 0.069727 | -97.66 | 3.078 | 0.0794 |
| 2 | Sperp(1,0)/2 | P2 | 0.062478 | 0.000135 | 0.062506 | -0.21 | 0.069727 | -53.74 | 1.013 | 0.314 |
| 2 | Szz(2,0) | P2 | 0.066829 | 0.000067 | 0.066870 | -0.62 | 0.069088 | -33.94 | 5.955 | 0.0147 |
| 2 | Sperp(2,0)/2 | P2 | 0.066883 | 0.000170 | 0.066870 | 0.08 | 0.069088 | -12.94 | 0.150 | 0.699 |
| 2 | Szz(3,0) | P2 | 0.062473 | 0.000074 | 0.062506 | -0.45 | 0.069727 | -97.66 | 3.078 | 0.0794 |
| 2 | Sperp(3,0)/2 | P2 | 0.062478 | 0.000135 | 0.062506 | -0.21 | 0.069727 | -53.74 | 1.013 | 0.314 |
| 2 | Szz(0,1) | P2 | 0.250344 | 0.000177 | 0.250307 | 0.21 | 0.249378 | 5.45 | 0.716 | 0.398 |
| 2 | Sperp(0,1)/2 | P2 | 0.250078 | 0.000210 | 0.250307 | -1.10 | 0.249378 | 3.33 | 0.129 | 0.719 |
| 2 | Szz(1,1) | P2 | 0.286491 | 0.000158 | 0.286376 | 0.72 | 0.264594 | 138.76 | 0.464 | 0.496 |
| 2 | Sperp(1,1)/2 | P2 | 0.286313 | 0.000148 | 0.286376 | -0.43 | 0.264594 | 146.47 | 1.239 | 0.266 |
| 2 | Szz(2,1) | P2 | 0.354034 | 0.000321 | 0.354115 | -0.25 | 0.359740 | -17.78 | 4.938 | 0.0263 |
| 2 | Sperp(2,1)/2 | P2 | 0.354451 | 0.000291 | 0.354115 | 1.15 | 0.359740 | -18.15 | 0.011 | 0.916 |
| 2 | Szz(3,1) | P2 | 0.286491 | 0.000158 | 0.286376 | 0.72 | 0.264594 | 138.76 | 0.464 | 0.496 |
| 2 | Sperp(3,1)/2 | P2 | 0.286313 | 0.000148 | 0.286376 | -0.43 | 0.264594 | 146.47 | 1.239 | 0.266 |
| 4 | E/N | P1 | -1.246310 | 0.000405 | -1.245433 | -2.16 | -1.249301 | 7.38 | 0.247 | 0.619 |
| 4 | D | P1,N1 | 0.129683 | 0.000051 | 0.129759 | -1.49 | 0.140756 | -217.45 | 0.844 | 0.358 |
| 4 | Szz(Q) | P1 | 0.426648 | 0.000421 | 0.426451 | 0.47 | 0.430096 | -8.20 | 0.001 | 0.974 |
| 4 | Sperp(Q)/2 | P1 | 0.427807 | 0.001157 | 0.426451 | 1.17 | 0.430096 | -1.98 | 0.433 | 0.511 |
| 4 | Szz(0,0) | P2 | 0.014499 | 0.000099 | 0.014542 | -0.43 | 0.030157 | -157.70 | 0.275 | 0.6 |
| 4 | Sperp(0,0)/2 | P2 | 0.014636 | 0.000594 | 0.014542 | 0.16 | 0.030157 | -26.14 | 0.033 | 0.855 |
| 4 | Szz(1,0) | P2,N1 | 0.019086 | 0.000090 | 0.019021 | 0.72 | 0.045827 | -295.84 | 1.106 | 0.293 |
| 4 | Sperp(1,0)/2 | P2 | 0.018838 | 0.000392 | 0.019021 | -0.47 | 0.045827 | -68.90 | 0.177 | 0.674 |
| 4 | Szz(2,0) | P2 | 0.023264 | 0.000104 | 0.023283 | -0.18 | 0.047290 | -231.49 | 1.658 | 0.198 |
| 4 | Sperp(2,0)/2 | P2 | 0.023717 | 0.000349 | 0.023283 | 1.24 | 0.047290 | -67.54 | 2.499 | 0.114 |
| 4 | Szz(3,0) | P2 | 0.019086 | 0.000090 | 0.019021 | 0.72 | 0.045827 | -295.84 | 1.106 | 0.293 |
| 4 | Sperp(3,0)/2 | P2 | 0.018838 | 0.000392 | 0.019021 | -0.47 | 0.045827 | -68.90 | 0.177 | 0.674 |
| 4 | Szz(0,1) | P2 | 0.305154 | 0.000362 | 0.304482 | 1.85 | 0.272357 | 90.49 | 0.043 | 0.836 |
| 4 | Sperp(0,1)/2 | P2 | 0.305190 | 0.000459 | 0.304482 | 1.54 | 0.272357 | 71.48 | 2.617 | 0.106 |
| 4 | Szz(1,1) | P2,N1 | 0.336868 | 0.000179 | 0.337083 | -1.20 | 0.282711 | 301.78 | 0.042 | 0.838 |
| 4 | Sperp(1,1)/2 | P2 | 0.336342 | 0.000625 | 0.337083 | -1.19 | 0.282711 | 85.81 | 0.451 | 0.502 |
| 4 | Szz(2,1) | P2 | 0.426648 | 0.000421 | 0.426451 | 0.47 | 0.430096 | -8.20 | 0.001 | 0.974 |
| 4 | Sperp(2,1)/2 | P2 | 0.427807 | 0.001157 | 0.426451 | 1.17 | 0.430096 | -1.98 | 0.433 | 0.511 |
| 4 | Szz(3,1) | P2 | 0.336868 | 0.000179 | 0.337083 | -1.20 | 0.282711 | 301.78 | 0.042 | 0.838 |
| 4 | Sperp(3,1)/2 | P2 | 0.336342 | 0.000625 | 0.337083 | -1.19 | 0.282711 | 85.81 | 0.451 | 0.502 |

| beta | observable | dtau=0.1 | dtau=0.05 | dtau=0.025 |
| ---: | --- | ---: | ---: | ---: |
| 2 | E/N | -1.155639 ± 0.000361 | -1.132297 ± 0.000339 | -1.127015 ± 0.000375 |
| 2 | D | 0.137704 ± 0.000047 | 0.140973 ± 0.000049 | 0.141918 ± 0.000040 |
| 2 | Szz(Q) | 0.361595 ± 0.000391 | 0.355174 ± 0.000403 | 0.354968 ± 0.000365 |
| 2 | Sperp(Q)/2 | 0.361653 ± 0.000229 | 0.356273 ± 0.000294 | 0.354871 ± 0.000396 |
| 4 | E/N | -1.280095 ± 0.000523 | -1.254888 ± 0.000381 | -1.248212 ± 0.000567 |
| 4 | D | 0.124571 ± 0.000045 | 0.128421 ± 0.000041 | 0.129272 ± 0.000110 |
| 4 | Szz(Q) | 0.433261 ± 0.000351 | 0.428281 ± 0.000680 | 0.427068 ± 0.000442 |
| 4 | Sperp(Q)/2 | 0.433762 ± 0.001118 | 0.430108 ± 0.001482 | 0.427688 ± 0.001315 |
| 4 | Szz(1,0) | 0.017938 ± 0.000131 | 0.018695 ± 0.000116 | 0.019073 ± 0.000101 |
| 4 | Szz(1,1) | 0.343444 ± 0.000308 | 0.338567 ± 0.000288 | 0.337263 ± 0.000185 |

P1 8/8 (|z| < 3.5), P2 32/32 (|z| < 4.0), N1 3/3 (|z P/P| > 5.0).

verdict: PASS

## Scope

This validates the antiperiodic lattice and its S(q) on 4x2 at U=4 and beta <= 4.
Larger lattices, lower temperatures and stronger coupling are not validated here.
