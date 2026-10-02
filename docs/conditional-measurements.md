---
date: 2026-09-30
datetime: 2026-09-30 13:30 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Optional conditional local measurements of double occupancy and hopping energy.
  Defines the estimator, slot ownership, bin output, comparison workflow, and limits.
---

# Conditional local measurements

Set `conditional_measure=1` to record additional estimates of double occupancy
and energy during local updates. The default is `0`. The option supports the
real, half-filled, particle-hole-symmetric Hubbard model on bipartite lattices,
with zero diagonal hopping and `U >= 0`. It works with forward or alternating
sweeps, optional global updates, fixed temperatures or parallel tempering,
and serial, OpenMP, MPI, or hybrid execution.

This is a comparison option: the existing scalar and spin outputs still contain
the ordinary end-of-sweep measurements. A `replica_bin_file` is required to save
the additional estimates. For example, append to a supported input:

```text
conditional_measure=1
replica_bin_file=bins.tsv
```

No random numbers are drawn for these measurements, and the update proposals,
acceptance rule, field, and delayed Green state are unchanged. Measurements are
enabled after warmup. A numerical failure of a new measurement fails the run;
it is never handled by dropping, clipping, or replacing the sample.

## Estimator and timing

Just before proposing the flip of HS variable `s[l,i]`, let `g=Gu[i,i]` denote
the effective up-spin Green at that variable's time cut, including pending
delayed updates. For `a=exp(-2*lambda*s[l,i])`, the determinant ratios are

```text
Ru = 1 + (a - 1)*(1 - g)
Rd = Ru/a
r  = Ru*Rd
```

Hold all other HS variables fixed and average over the two values of this
variable with their conditional probabilities. Its local doublon estimator is

```text
d_cond = 2*g*(1-g)/(1+r).
```

For nonzero flip ratios, the explicitly flipped local doublon is `d/r`,
where `d=g*(1-g)`. Thus `(d+r*d_flipped)/(1+r)` gives the expression above.
For `j != i`, the two up-spin Green directions transform as
`Gu_flipped[j,i]=Gu[j,i]/Rd` and `Gu_flipped[i,j]=Gu[i,j]/Ru`.
Applying the same two-state weighting gives the hopping formula below.

This formula applies to the observable at the matching site and time cut;
it must not be applied to a previously averaged total doublon or a spin
correlation. For `U > 0` and fixed positive `dtau`, it obeys

```text
-1/[2*(exp(dtau*U/2)-1)] <= d_cond <= 1/[2*(exp(dtau*U/2)+1)].
```

The lower bound is not uniform as `dtau` tends to zero. Negative conditional
values remain possible. `U=0` is supported directly without dividing by this
singular lower-bound expression.

The bound follows directly from the rational estimator. Let
`c=cosh(lambda)=exp(dtau*U/2)`, `s=sinh(lambda)`, and
`x=sign(log(a))*(2*g-1)`. Then `r=(c-s*x)^2` and
`d_cond=(1-x*x)/(2*(1+r))`. For `U>0`, the two inequalities reduce to
nonnegative squares:

```text
1+r - (c+1)*(1-x*x) = c*(sqrt(c-1)-sqrt(c+1)*x)^2 >= 0
1+r + (c-1)*(1-x*x) = c*(sqrt(c+1)-sqrt(c-1)*x)^2 >= 0.
```

For real symmetric hopping `t_ij`, assign to site `i` the hopping contribution

```text
k_i      = -sum_j t_ij*(Gu[j,i] + Gu[i,j])
k_i_cond = -sum_j t_ij*((1+Ru)*Gu[j,i] + (1+Rd)*Gu[i,j])/(1+r).
```

The diagonal hopping is zero. The implementation reads the effective delayed
Green without flushing it, and scales numerator and denominator to avoid
forming `g*g` or `Ru*Rd` in the measurement when these products would overflow.
There is no analogous uniform bound established here for `k_i_cond` or energy.

Within each sweep, every site/slice contributes once, whether its proposed
flip is accepted or rejected. With `N` sites and `Ltr` time slices:

```text
D_cond    = sum_(l,i) d_cond / (N*Ltr)       # per site
K_cond    = sum_(l,i) k_i_cond / Ltr         # total
Ehub_cond = K_cond + U*N*D_cond              # total
```

K and D are measured at the same stages, including their covariance. Combining
the ordinary end-of-sweep K with the new D can increase the energy variance.
The new measurements are taken before the subsequent global pass and PT
exchange. They remain attached to the temperature **slot**, using that slot's
`dtau`; they do not follow the exchanged walker. Ordinary and conditional
measurements therefore need not describe the same instantaneous configuration.
Their equilibrium expectations agree. This does not remove equilibration bias.

## Output and analysis

When disabled, the legacy bin header and data format are unchanged. When
enabled, the first 20 columns are retained and these four columns are appended:

| Column | Meaning |
| --- | --- |
| `conditional_count` | Number of measurement sweeps in the bin |
| `sum_D_cond` | Sum of the sweep-averaged D per site |
| `sum_K_cond` | Sum of the total hopping energy estimates |
| `sum_Ehub_cond` | Sum of the total Hubbard energy estimates |

Divide the three sums by `conditional_count`. The count is a sweep count, not
the number of local HS variables. These channels require positive unit sign;
the ordinary sign-weighted columns retain their existing meaning.

Use the standard-library Python script to compare all retained bins:

```sh
python3 scripts/analyze_conditional_bins.py bins.tsv --expect-replicas 8 --expect-slots 1 > comparison.json
```

For a six-slot PT run, use `--expect-slots 6`. The expected counts detect
missing whole replicas or slots; missing/duplicate bins and inconsistent
counts, seeds, or energy conventions are also rejected. Python 3.10 or newer
is required for this optional analysis script.

The output includes independent-replica means and sample standard errors,
paired conditional-minus-ordinary differences, per-replica values, and bin
extrema. Each PT ladder is one independent unit at a given temperature;
different temperatures from that ladder are correlated. A single replica
has no independent-replica SE and is reported with `null`.

If the staggered spin channels were measured, the script also reports
`2*Szz(Q)` as an equilibrium estimate of `Sperp(Q)` and `3*Szz(Q)` as an
estimate of `Stot(Q)`, along with the direct estimates and paired SU(2)
residual. It does not change the spin measurements. Szz can itself have heavy
tails, and SU(2) consistency is a separate equilibrium check.

## Validation and limits

The fast C tests compare the conditional formulas with explicit flipped
configurations using independent two-spin 2x2 products over all 256 HS fields,
including `U=0`, delayed-Green reads, and independent Fock-space reference
values. The [standalone reference generator](../data/conditional_measurements/README.md)
reproduces the Fock-space comparison with NumPy and SciPy. The tests also check
field/RNG/Green preservation and that accepted PT
exchanges leave the local measurement accumulators in their original slots.
The optional slow sampling test checks the integrated fixed-beta and PT
drivers against exact finite-time-step expectations with independent series.

CLI tests verify the additional output, unchanged ordinary measurements within
each build, and agreement across the four parallel builds. Integer identities,
seeds, and acceptance counts agree exactly; floating-point arithmetic between
different compilers is compared with a numerical tolerance.

Variance reduction of one conditional observable does not establish a lower
integrated Monte Carlo error or better efficiency per unit time. Assess the
resulting correlations, measurement cost, independent-series stability,
initialization dependence, and finite-`dtau` effects for the target run.
Historical bin sums do not contain the local Green values needed to recover
these estimators: adopting them requires a new measurement run.

Synchronizing local updates and measurements is related to M. Ulybyshev and
F. Assaad, “Mitigating spikes in fermion Monte Carlo methods by reshuffling
measurements,” *Phys. Rev. E* **106**, 025318 (2022),
[doi:10.1103/PhysRevE.106.025318](https://doi.org/10.1103/PhysRevE.106.025318).
The HS representation and the particular conditional formulas above must be
distinguished from the paper's implementation. No claim of infinite variance
for a finite discrete HS system follows from these diagnostics.
