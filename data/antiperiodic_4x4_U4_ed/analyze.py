#!/usr/bin/env python3
"""Audit all registered runs and compare independent-seed estimates with ED."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

from run_comparison import cases, input_text

HERE = Path(__file__).resolve().parent
N = 16
OBS = ('E_per_site', 'D', 'Szz_Q', 'Sperp_Q', 'S_Q', 'SU2_Q', 'Szz_0')


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def mean_se(xs):
    return statistics.mean(xs), statistics.stdev(xs)/math.sqrt(len(xs))


def require(ok, message):
    if not ok:
        raise ValueError(message)


def rows(path):
    return [line.split() for line in path.read_text().splitlines()
            if line.strip() and not line.startswith('#')]


def close(x, y, tol=2e-11):
    return math.isclose(x, y, rel_tol=tol, abs_tol=tol)


def matrix_check(path):
    got = [float(x) for x in path.read_text().split()]
    require(len(got) == 257 and got[0] == 16, 'matrix dimensions')
    expected = [[0.] * 16 for _ in range(16)]
    for y in range(4):
        for x in range(4):
            i = x+4*y
            for j, t in [(((x+1)%4)+4*y, 1 if x == 3 else -1),
                         (x+4*((y+1)%4), -1)]:
                expected[i][j] += t
                expected[j][i] += t
    require(got[1:] == [v for row in expected for v in row], 'AP/P hopping mismatch')


def inspect(c, binary_sha):
    d = HERE/'runs'/c['name']
    require((d/'input.in').read_text() == input_text(c), 'input content')
    require(sha(d/'input.in') == c['input_sha256'], 'input hash')
    start = json.loads((d/'started.json').read_text())
    done = json.loads((d/'completed.json').read_text())
    require(start['binary_sha256'] == binary_sha and start['input_sha256'] == c['input_sha256'], 'run binding')
    require(done['case'] == c['name'] and done['exit_code'] == 0, 'run failed')
    require(set(done['outputs']) == {'bins.tsv','hopping_used.txt','observables.dat',
            'szz.dat','sperp.dat','stdout.txt','stderr.txt'}, 'output inventory')
    for name, digest in done['outputs'].items():
        require(sha(d/name) == digest, 'output hash: '+name)
    require(not (d/'stderr.txt').read_text().strip(), 'nonempty stderr')
    matrix_check(d/'hopping_used.txt')
    lines = (d/'bins.tsv').read_text().splitlines()
    header = dict(s.split('=') for s in next(l[2:] for l in lines if l.startswith('# lattice=')).split())
    for k, v in dict(lattice='square', Lx='4', Ly='4', n='16', bc_x='antiperiodic',
                     bc_y='periodic', U='4', nwarm='5000', nmeas='20000', nbin='20',
                     global_update='none', global_interval='100').items():
        require(header[k] == v, 'bin header '+k)
    require(close(float(header['dtau']), float(c['dtau'])), 'bin dtau')
    require('# szz_Q_index=10 szz_0_index=0 sperp_Q_index=10' in lines, 'Q indices')
    expected_cols = ('beta_index beta_requested beta_effective Ltr replica_id seed bin_id '
                     'sweep_begin sweep_end count sum_sign sum_sign_Ehub sum_sign_D '
                     'local_accepted local_attempts global_accepted global_attempts '
                     'sum_sign_Szz_Q sum_sign_Sperp_Q sum_sign_Szz_0').split()
    require(next(l[11:].split() for l in lines if l.startswith('# columns: ')) == expected_cols, 'bin columns')
    raw = rows(d/'bins.tsv')
    require(len(raw) == 20 and all(len(r) == 20 for r in raw), 'bin shape')
    values = {o: [] for o in OBS}
    for b, r in enumerate(raw):
        require(all(math.isfinite(float(x)) for x in r), 'nonfinite bin')
        for i, want in [(0,0),(3,round(c['beta']/float(c['dtau']))),(4,0),(5,c['seed']),
                        (6,b),(7,1000*b+1),(8,1000*(b+1)),(9,1000),(15,0),(16,0)]:
            require(int(r[i]) == want, 'bin integer metadata')
        require(float(r[1]) == float(r[2]) == c['beta'], 'bin beta')
        require(float(r[10]) == 1000., 'sign is not one')
        require(0 <= int(r[13]) <= int(r[14]) == 16*round(c['beta']/float(c['dtau']))*1000, 'acceptance counts')
        e, dval, z, p, z0 = (float(r[k])/1000 for k in (11,12,17,18,19))
        for key, val in zip(OBS, (e/16,dval,z,p,z+p,p-2*z,z0)):
            values[key].append(val)
    spin = []
    for channel in ('szz','sperp'):
        rr = rows(d/(channel+'.dat'))
        require(len(rr) == 16 and all(len(r) == 12 for r in rr), 'spin shape')
        for q, row in enumerate(rr):
            require(all(math.isfinite(float(x)) for x in row), 'nonfinite spin')
            require(int(row[3]) == q and int(row[4]) == q%4 and int(row[5]) == q//4, 'spin momentum')
            require(float(row[0]) == float(row[1]) == c['beta'], 'spin beta')
        spin.append([float(row[10]) for row in rr])
    z, p = spin
    means = {o: statistics.mean(values[o]) for o in OBS}
    require(close(z[10], means['Szz_Q']) and close(p[10], means['Sperp_Q']) and close(z[0],means['Szz_0']), 'spin/bin agreement')
    require(close(sum(z), 16*(1-2*means['D'])/4) and close(sum(p),2*sum(z)), 'spin sum rules')
    scalar = rows(d/'observables.dat')
    require(len(scalar) == 1 and len(scalar[0]) == 14, 'scalar shape')
    s = [float(x) for x in scalar[0]]
    require(all(math.isfinite(x) for x in s), 'scalar finite')
    require(abs(s[1]/16-means['E_per_site']) < 1e-7 and abs(s[9]-means['D']) < 1e-8, 'scalar/bin agreement')
    require(s[7] == 16 and s[11] == 1, 'density/sign')
    return dict(case=c, means=means, bins=values, spin_su2=[b-2*a for a,b in zip(z,p)],
                walltime=done['elapsed_seconds'])


def fit(points):
    # Center x to improve numerical conditioning; absolute SE, no chi2 scaling.
    w = [1/p['se']**2 for p in points]
    x = [float(p['dtau'])**2 for p in points]
    y = [p['mean'] for p in points]
    sw = sum(w)
    xm = sum(a*b for a,b in zip(w,x))/sw
    ym = sum(a*b for a,b in zip(w,y))/sw
    sxx = sum(a*(b-xm)**2 for a,b in zip(w,x))
    b = sum(a*(xx-xm)*(yy-ym) for a,xx,yy in zip(w,x,y))/sxx
    a = ym-b*xm
    se = math.sqrt(1/sw+xm*xm/sxx)
    chi2 = sum(ww*(yy-a-b*xx)**2 for ww,xx,yy in zip(w,x,y))
    p = math.erfc(math.sqrt(chi2/2)) if len(points) == 3 else None
    return dict(mean=a,se=se,slope=b,chi2=chi2,dof=len(points)-2,p=p)


def write_tsv(path, data):
    with path.open('w', newline='') as f:
        w = csv.DictWriter(f,fieldnames=list(data[0]),delimiter='\t',lineterminator='\n')
        w.writeheader()
        w.writerows(data)


def main():
    reg = json.loads((HERE/'registration.json').read_text())
    require(len(reg['cases']) == 36, 'case count')
    require([{k:c[k] for k in ('name','beta','dtau','series','seed')} for c in reg['cases']] == list(cases()), 'case registration')
    runs = [inspect(c,reg['binary_sha256']) for c in reg['cases']]
    reference = json.loads((HERE/'ed_reference.json').read_text())
    source = json.loads((HERE/'ed_source_summary.json').read_text())
    require(sha(HERE/'ed_source_summary.json') == reference['source_case_summary_sha256'], 'ED source hash')
    require(sha(HERE/'ed_stan.in') == reference['ed_input_sha256'], 'ED input hash')
    require(reference['physics'] == source['physics'] and reference['observables'] == source['observables'], 'ED copy')
    physics = source['physics']
    require(physics['shape'] == [4,4] and physics['sites'] == 16, 'ED shape')
    require(physics['filling']['n_up'] == physics['filling']['n_down'] == 8, 'ED sector')
    require([r['type'] for r in physics['boundary_conditions']] == ['antiperiodic','periodic'], 'ED boundaries')
    require(physics['hamiltonian']['onsite_u'] == 4 and physics['hamiltonian']['interaction_convention'] == 'U_n_up_n_down', 'ED interaction')
    require(physics['thermodynamics']['state'] == 'ground' and source['diagnostics']['lobpcg_convergence']['converged'], 'ED status')
    ref = reference['observables']
    require(close(ref['energy_total']/16,ref['energy_per_site']), 'ED energy normalization')
    require(close(1-2*ref['double_occupancy_per_site'],ref['local_moment_per_site']), 'ED local moment')
    sq = ref['spin_structure_factor_pi_pi']['re']
    ed = dict(E_per_site=ref['energy_per_site'],D=ref['double_occupancy_per_site'],
              Szz_Q=sq/3,Sperp_Q=2*sq/3,S_Q=sq,SU2_Q=0.,Szz_0=0.)
    grouped, qchecks, drift = [], [], []
    for beta in (4,8,16):
        for dtau in ('0.1','0.05','0.025'):
            group = [r for r in runs if r['case']['beta'] == beta and r['case']['dtau'] == dtau]
            require(len(group)==4, 'seed count')
            for o in OBS:
                mean, se = mean_se([r['means'][o] for r in group])
                pooled = [x for r in group for x in r['bins'][o]]
                blocks = {k:[statistics.mean(r['bins'][o][i:i+k]) for r in group
                             for i in range(0,20,k)] for k in (1,2,4,5)}
                grouped.append(dict(beta=beta,dtau=dtau,observable=o,mean=mean,se=se,
                                    ed=ed[o],z_ed=(mean-ed[o])/se,
                                    **{f'bin_{k*1000}_se':mean_se(v)[1] for k,v in blocks.items()}))
                changes = [statistics.mean(r['bins'][o][10:])-statistics.mean(r['bins'][o][:10]) for r in group]
                dm, ds = mean_se(changes)
                drift.append(dict(beta=beta,dtau=dtau,observable=o,second_minus_first=dm,se=ds,z=dm/ds))
            for q in range(16):
                m,s = mean_se([r['spin_su2'][q] for r in group])
                qchecks.append(dict(beta=beta,dtau=dtau,mx=q%4,my=q//4,delta=m,se=s,z=m/s))
    fits, fine = [], []
    for beta in (4,8,16):
        for o in OBS:
            points = [r for r in grouped if r['beta'] == beta and r['observable'] == o]
            f = fit(points)
            fits.append(dict(beta=beta,observable=o,**f,ed=ed[o],z_ed=(f['mean']-ed[o])/f['se']))
            g = fit(points[1:])
            fine.append(dict(beta=beta,observable=o,**g,ed=ed[o],z_ed=(g['mean']-ed[o])/g['se']))
    plateau=[]
    for o in OBS:
        a,b = [next(r for r in fits if r['beta']==beta and r['observable']==o) for beta in (8,16)]
        diff=b['mean']-a['mean']; se=math.hypot(a['se'],b['se'])
        plateau.append(dict(observable=o,b16_minus_b8=diff,se=se,z=diff/se))
    write_tsv(HERE/'group_estimates.tsv', grouped)
    write_tsv(HERE/'dtau_extrapolated.tsv', fits)
    write_tsv(HERE/'fine_two_point.tsv', fine)
    write_tsv(HERE/'temperature_comparison.tsv', plateau)
    write_tsv(HERE/'spin_su2_all_q.tsv', qchecks)
    write_tsv(HERE/'half_run_drift.tsv', drift)
    write_tsv(HERE/'seed_estimates.tsv', [dict(**r['case'],**r['means']) for r in runs])
    result=dict(integrity='PASS', runs=36,bins=720,measurements=720000,
                sign=1., density=1., matrix='Independent signed-bond construction matches every run',
                statistics='SE of four independent seed means; all seeds retained; paired Szz/Sperp differences',
                total_cpu_run_seconds=sum(r['walltime'] for r in runs),
                min_three_point_fit_p=min(r['p'] for r in fits),
                max_abs_su2_Q_z=max(abs(r['z']) for r in qchecks if (r['mx'],r['my'])==(2,2)),
                max_abs_all_q_su2_z=max(abs(r['z']) for r in qchecks),
                low_temperature_fits=[r for r in fits if r['beta']==16],plateau=plateau,
                scope='Exploratory comparison. No claim of rigorous ground-state convergence or of a formal pass/fail physics gate.')
    (HERE/'analysis.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
