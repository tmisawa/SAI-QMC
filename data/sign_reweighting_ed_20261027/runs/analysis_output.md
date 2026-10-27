# DQMC versus ED: sign-reweighted estimator

z = (QMC - ED) / err at dtau 0.05 and 0.025; z0 uses (4 X(0.025) - X(0.05)) / 3 as a diagnostic of the step dependence. Rule fixed before the runs: |z0| <= 3.

| case | beta | sign (dtau 0.05) | sign (dtau 0.025) | E_hub z / z / z0 | ntot z / z / z0 | doublon z / z / z0 | Szz max abs z0 |
| --- | ---: | ---: | ---: | --- | --- | --- | ---: |
| L4_periodic | 1 | 1.0000(0e+00) | 1.0000(0e+00) | -2.4 / 0.4 / 0.9 | 2.2 / -1.8 / -2.3 | -5.0 / -0.9 / 0.3 | 1.7 |
| L4_periodic | 2 | 0.9999(3e-05) | 0.9999(4e-05) | -3.5 / 1.3 / 2.1 | 0.1 / -1.2 / -1.2 | -2.4 / 0.1 / 0.7 | 1.3 |
| L4_periodic | 4 | 0.9862(5e-04) | 0.9858(6e-04) | -3.0 / -1.5 / -0.8 | 0.1 / 1.5 / 1.5 | -3.8 / -0.5 / 0.5 | 1.0 |
| L4_periodic | 10 | 0.9930(6e-04) | 0.9911(7e-04) | -2.8 / -1.1 / -0.3 | 1.1 / -1.5 / -1.7 | -2.9 / -1.1 / -0.3 | 1.7 |
| L4_antiperiodic | 1 | 1.0000(0e+00) | 1.0000(0e+00) | -1.7 / -0.4 / 0.0 | 1.1 / -1.4 / -1.6 | -3.6 / -0.6 / 0.3 | 1.3 |
| L4_antiperiodic | 2 | 0.9998(5e-05) | 0.9999(5e-05) | -3.6 / 0.6 / 1.5 | 1.3 / 0.9 / 0.6 | -0.6 / -0.5 / -0.3 | 0.7 |
| L4_antiperiodic | 4 | 0.9508(1e-03) | 0.9495(1e-03) | -1.4 / 0.8 / 1.1 | 0.9 / 1.5 / 1.2 | -2.4 / 0.5 / 1.0 | 1.6 |
| L4_antiperiodic | 10 | 0.3995(5e-03) | 0.4102(4e-03) | 0.8 / -0.6 / -0.8 | 2.3 / -0.0 / -0.5 | -0.4 / -0.7 / -0.5 | 1.9 |
| sq4x2_periodic_mu2 | 1 | 1.0000(0e+00) | 1.0000(0e+00) | -8.9 / -1.0 / 1.0 | -1.1 / -0.5 / -0.2 | -14.6 / -6.0 / -2.0 | 1.4 |
| sq4x2_periodic_mu2 | 2 | 0.9999(3e-05) | 0.9999(4e-05) | -11.0 / -1.9 / 0.7 | 0.9 / 1.4 / 1.2 | -13.9 / -4.1 / -0.4 | 2.0 |
| sq4x2_periodic_mu2 | 4 | 0.9742(7e-04) | 0.9747(7e-04) | -6.4 / -2.1 / -0.3 | 3.7 / 0.4 / -0.6 | -12.8 / -2.6 / 0.7 | 1.2 |
| sq4x2_periodic_mu1 | 1 | 1.0000(0e+00) | 1.0000(0e+00) | -10.2 / -1.8 / 0.7 | 0.9 / 0.8 / 0.5 | -14.4 / -4.9 / -1.2 | 1.4 |
| sq4x2_periodic_mu1 | 2 | 1.0000(1e-05) | 0.9999(3e-05) | -9.7 / -3.8 / -1.8 | 2.4 / 0.7 / 0.1 | -17.1 / -3.7 / 0.1 | 0.9 |
| sq4x2_periodic_mu1 | 4 | 0.9990(1e-04) | 0.9991(1e-04) | -5.6 / -3.6 / -2.1 | 2.0 / 1.1 / 0.6 | -18.0 / -1.2 / 3.2 **(>3)** | 1.9 |
| sq4x2_antiperiodic_mu2 | 1 | 1.0000(0e+00) | 1.0000(0e+00) | -7.3 / -2.2 / -0.1 | -0.4 / -1.6 / -1.5 | -13.8 / -2.7 / 1.1 | 1.8 |
| sq4x2_antiperiodic_mu2 | 2 | 0.9998(5e-05) | 0.9998(5e-05) | -9.9 / -1.2 / 1.4 | 3.0 / -0.8 / -1.5 | -14.2 / -4.9 / -0.9 | 1.4 |
| sq4x2_antiperiodic_mu2 | 4 | 0.9908(3e-04) | 0.9910(4e-04) | -9.5 / -2.5 / -1.0 | 2.3 / -0.3 / -0.8 | -13.8 / -3.6 / -2.0 | 1.5 |
| sq4x2_antiperiodic_mu1 | 1 | 1.0000(0e+00) | 1.0000(0e+00) | -7.4 / -1.5 / 0.2 | 0.5 / -2.4 / -2.5 | -16.1 / -5.1 / -0.7 | 1.8 |
| sq4x2_antiperiodic_mu1 | 2 | 0.9996(7e-05) | 0.9997(7e-05) | -8.1 / -1.1 / 0.9 | 3.4 / 1.1 / 0.3 | -15.7 / -2.3 / 1.1 | 0.4 |
| sq4x2_antiperiodic_mu1 | 4 | 0.9536(1e-03) | 0.9514(9e-04) | -5.1 / -1.2 / 0.2 | 3.3 / 1.7 / 0.9 | -11.6 / -0.8 / 1.8 | 1.3 |

187 of 188 comparisons satisfy |z0| <= 3; 1 do not.

## Post-hoc follow-up (not part of the pre-registered verdict)

The one comparison outside the rule is the doublon of `sq4x2_periodic_mu1` at beta=4
(z0 = 3.2). After the verdict was recorded, the same input was run once more at dtau=0.0125
(`followup/sq4x2_periodic_mu1_dt0.0125`, same seed and statistics):

| beta | observable | ED | dtau 0.05 | dtau 0.025 | dtau 0.0125 |
| ---: | --- | ---: | --- | --- | --- |
| 1 | doublon | 0.093578 | 0.092891(48), z=-14.4 | 0.093346(48), z=-4.9 | 0.093538(47), z=-0.8 |
| 2 | doublon | 0.091786 | 0.090866(54), z=-17.1 | 0.091564(59), z=-3.7 | 0.091684(58), z=-1.8 |
| 4 | doublon | 0.085696 | 0.084595(61), z=-18.0 | 0.085623(61), z=-1.2 | 0.085620(65), z=-1.2 |
| 4 | E_hub | -13.912742 | -13.951016(6831), z=-5.6 | -13.936811(6750), z=-3.6 | -13.914142(7577), z=-0.2 |
| 4 | ntot | 6.073663 | 6.074514(427), z=2.0 | 6.074166(442), z=1.1 | 6.073223(482), z=-0.9 |

At the finest step every value is within 1.8 errors of ED. The two-step combination
overshoots because the dtau=0.025 doublon at beta=4 lies about 3 errors above the value
expected from pure dtau^2 scaling of the dtau=0.05 bias. This is consistent with a
statistical excursion (about 0.5 of 188 comparisons are expected beyond 3 errors) or a
higher-order step dependence; two steps do not separate these, which is why they are not
used to adopt a dtau -> 0 value.
