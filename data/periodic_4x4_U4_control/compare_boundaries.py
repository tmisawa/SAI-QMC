#!/usr/bin/env python3
"""Matched-seed P/P control; preserve the frozen AP/P dataset unchanged."""
import csv
import json
import math
from pathlib import Path
import statistics

import analyze as pp

HERE = Path(__file__).resolve().parent
APP = HERE.parent/'antiperiodic_4x4_U4_ed'


def read(path):
    with path.open() as f:
        return list(csv.DictReader(f, delimiter='\t'))


def verify_app(reg):
    pp.require(pp.sha(APP/'SHA256SUMS') == reg['app_manifest_sha256'], 'AP/P manifest changed')
    pp.require(pp.sha(APP/'registration.json') == reg['app_registration_sha256'], 'AP/P registration changed')
    count=0
    for line in (APP/'SHA256SUMS').read_text().splitlines():
        digest,name=line.split('  ',1)
        pp.require(pp.sha(APP/name) == digest, 'AP/P file changed: '+name)
        count+=1
    app_reg=json.loads((APP/'registration.json').read_text())
    pp.require(app_reg['binary_sha256']==reg['binary_sha256'], 'different executables')
    pp.require(len(app_reg['cases'])==len(reg['cases'])==36, 'case count')
    for a,p in zip(app_reg['cases'],reg['cases']):
        for key in ('name','beta','dtau','series','seed'):
            pp.require(a[key]==p[key], 'unmatched case '+key)
        want=(APP/'runs'/a['name']/'input.in').read_text().replace('bc_x=antiperiodic','bc_x=periodic')
        pp.require(want==(HERE/'runs'/p['name']/'input.in').read_text(), 'more than boundary changed')
    return count


def reference(root):
    r=json.loads((root/'ed_reference.json').read_text())['observables']
    sq=r['spin_structure_factor_pi_pi']['re']
    return dict(E_per_site=r['energy_per_site'],D=r['double_occupancy_per_site'],
                Szz_Q=sq/3,Sperp_Q=2*sq/3,S_Q=sq,SU2_Q=0.,Szz_0=0.)


def main():
    reg=json.loads((HERE/'registration.json').read_text())
    app_count=verify_app(reg)
    # Re-audit every P/P raw run without modifying the AP/P analysis or files.
    for c in reg['cases']:
        pp.inspect(c,reg['binary_sha256'])
    seeds={label:{r['name']:r for r in read(root/'seed_estimates.tsv')}
           for label,root in [('AP/P',APP),('P/P',HERE)]}
    refs={label:reference(root) for label,root in [('AP/P',APP),('P/P',HERE)]}
    groups=[]
    for beta in (4,8,16):
        for dtau in ('0.1','0.05','0.025'):
            cases=[c for c in reg['cases'] if c['beta']==beta and c['dtau']==dtau]
            for obs in pp.OBS:
                a=[float(seeds['AP/P'][c['name']][obs]) for c in cases]
                p=[float(seeds['P/P'][c['name']][obs]) for c in cases]
                am,ase=pp.mean_se(a); pm,pse=pp.mean_se(p)
                diff=[x-y for x,y in zip(a,p)]
                dm,dse=pp.mean_se(diff)
                cov=sum((x-am)*(y-pm) for x,y in zip(a,p))/(4*3)
                pp.require(pp.close(dse*dse,ase*ase+pse*pse-2*cov), 'paired covariance')
                eddiff=refs['AP/P'][obs]-refs['P/P'][obs]
                groups.append(dict(beta=beta,dtau=dtau,observable=obs,
                                   app_mean=am,app_se=ase,pp_mean=pm,pp_se=pse,
                                   app_minus_pp=dm,paired_se=dse,covariance_of_means=cov,
                                   ed_difference=eddiff,residual_difference=dm-eddiff,
                                   residual_z=(dm-eddiff)/dse))
    fits=[]
    for beta in (4,8,16):
        for obs in pp.OBS:
            rs=[r for r in groups if r['beta']==beta and r['observable']==obs]
            pts=[dict(dtau=r['dtau'],mean=r['residual_difference'],se=r['paired_se']) for r in rs]
            f=pp.fit(pts)
            fits.append(dict(beta=beta,observable=obs,**f,z=f['mean']/f['se']))
    # Keep both SE methods for both boundaries as diagnostics. The primary
    # independent-seed estimate remains unchanged; no method selected by outcome.
    method=[]
    overview=[]
    for label,root in [('AP/P',APP),('P/P',HERE)]:
        g=read(root/'group_estimates.tsv')
        ff=read(root/'dtau_extrapolated.tsv')
        for beta in (4,8,16):
            for obs in pp.OBS:
                rs=[r for r in g if int(r['beta'])==beta and r['observable']==obs]
                for se_key in ('se','bin_1000_se','bin_2000_se','bin_4000_se','bin_5000_se'):
                    f=pp.fit([dict(dtau=r['dtau'],mean=float(r['mean']),se=float(r[se_key])) for r in rs])
                    method.append(dict(boundary=label,beta=beta,observable=obs,error_method=se_key,
                                       **f,ed=refs[label][obs],z_ed=(f['mean']-refs[label][obs])/f['se']))
        su2=read(root/'spin_su2_all_q.tsv')
        drift=read(root/'half_run_drift.tsv')
        overview.append(dict(boundary=label,runs=36,bins=720,
            max_finite_step_su2_Q_z=max(abs(float(r['z'])) for r in su2 if (r['mx'],r['my'])==('2','2')),
            max_all_q_su2_z=max(abs(float(r['z'])) for r in su2),
            all_q_su2_nominal_gt3=sum(abs(float(r['z']))>3 for r in su2),
            all_q_su2_entries=len(su2),
            max_half_run_drift_z=max(abs(float(r['z'])) for r in drift),
            beta16=[r for r in ff if r['beta']=='16']))
    pp.write_tsv(HERE/'boundary_differences.tsv',groups)
    pp.write_tsv(HERE/'paired_residual_extrapolated.tsv',fits)
    pp.write_tsv(HERE/'error_method_comparison.tsv',method)
    summary=dict(integrity='PASS',matched_pairs=36,app_manifest_verified_files=app_count,
                 only_input_change='bc_x=antiperiodic -> bc_x=periodic',
                 primary_error_method='SE across four independent seeds per parameter; paired between boundaries',
                 interpretation='Exploratory diagnostic, with few-seed uncertainty and finite-temperature/systematic effects. '
                 'Nominal z ratios are not formal multiple-testing significance claims.',
                 boundaries=overview, beta16_paired_residuals=[r for r in fits if r['beta']==16])
    (HERE/'control_comparison.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    print(json.dumps(summary,indent=2))


if __name__=='__main__':
    main()
