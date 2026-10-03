#!/usr/bin/env python3
"""Plot finite-time-step estimates, extrapolations, and the ground-state reference."""
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = Path(__file__).resolve().parent


def read(name):
    with (HERE/name).open() as f:
        return list(csv.DictReader(f,delimiter='\t'))


def main():
    groups=read('group_estimates.tsv')
    fits=read('dtau_extrapolated.tsv')
    fig, axes=plt.subplots(1,3,figsize=(12,4),layout='constrained')
    obs=[('E_per_site',r'$E/N$'),('D',r'$D$'),('S_Q',r'$S(\pi,\pi)$')]
    colors=['#0072B2','#D55E00','#009E73']
    for ax,(o,label) in zip(axes,obs):
        ed=float(next(r['ed'] for r in groups if r['observable']==o))
        ax.axhline(ed,color='black',linestyle='--',linewidth=1.2,label='Ground-state ED')
        for beta,color,marker in zip(('4','8','16'),colors,('o','s','^')):
            rs=[r for r in groups if r['beta']==beta and r['observable']==o]
            f=next(r for r in fits if r['beta']==beta and r['observable']==o)
            x=[float(r['dtau'])**2 for r in rs]
            ax.errorbar(x,[float(r['mean']) for r in rs],yerr=[float(r['se']) for r in rs],
                        fmt=marker,color=color,capsize=3,label=fr'$\beta={beta}$')
            ax.plot([0,max(x)],[float(f['mean']),float(f['mean'])+float(f['slope'])*max(x)],color=color,alpha=.65,lw=1)
            ax.errorbar([0],[float(f['mean'])],yerr=[float(f['se'])],fmt=marker,
                        mfc='white',color=color,capsize=3)
        ax.set_xlabel(r'$\Delta\tau^2$')
        ax.set_ylabel(label)
        ax.grid(alpha=.18)
        ax.ticklabel_format(axis='y',useOffset=False)
    axes[0].legend(fontsize=9)
    fig.suptitle('4 x 4 Hubbard, U=4, half filling, x antiperiodic / y periodic\n'
                 '4 independent seeds per point; open symbols: quadratic time-step extrapolation',fontsize=12)
    for ext in ('png','svg'):
        fig.savefig(HERE/('comparison.'+ext),dpi=180)


if __name__=='__main__':
    main()
