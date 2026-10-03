#!/usr/bin/env python3
"""Compare both boundaries to their own ground-state ED, at the same conditions."""
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE=Path(__file__).resolve().parent
APP=HERE.parent/'antiperiodic_4x4_U4_ed'


def read(path):
    with path.open() as f:
        return list(csv.DictReader(f,delimiter='\t'))


def main():
    fig,axes=plt.subplots(2,3,figsize=(12,7),layout='constrained')
    obs=[('E_per_site',r'$E/N-E_{ED}/N$'),('S_Q',r'$S(Q)-S_{ED}(Q)$'),
         ('SU2_Q',r'$S_\perp(Q)-2S^{zz}(Q)$')]
    for row,(bc,root) in enumerate([('AP/P',APP),('P/P',HERE)]):
        groups=read(root/'group_estimates.tsv');fits=read(root/'dtau_extrapolated.tsv')
        for col,(o,label) in enumerate(obs):
            ax=axes[row,col];ax.axhline(0,color='black',ls='--',lw=1)
            for beta,color,marker in zip(('4','8','16'),('#0072B2','#D55E00','#009E73'),('o','s','^')):
                rs=[r for r in groups if r['beta']==beta and r['observable']==o]
                f=next(r for r in fits if r['beta']==beta and r['observable']==o)
                x=[float(r['dtau'])**2 for r in rs]
                y=[float(r['mean'])-float(r['ed']) for r in rs]
                ax.errorbar(x,y,yerr=[float(r['se']) for r in rs],color=color,fmt=marker,capsize=3,label=fr'$\beta={beta}$')
                a=float(f['mean'])-float(f['ed'])
                ax.plot([0,max(x)],[a,a+float(f['slope'])*max(x)],color=color,lw=1,alpha=.65)
                ax.errorbar([0],[a],yerr=[float(f['se'])],color=color,fmt=marker,mfc='white',capsize=3)
            ax.set_title(bc);ax.set_ylabel(label);ax.set_xlabel(r'$\Delta\tau^2$');ax.grid(alpha=.18)
    for col in range(3):
        ymin=min(ax.get_ylim()[0] for ax in axes[:,col]);ymax=max(ax.get_ylim()[1] for ax in axes[:,col])
        for ax in axes[:,col]:ax.set_ylim(ymin,ymax)
    axes[0,0].legend(fontsize=9)
    fig.suptitle('4 x 4, U=4: matched AP/P and P/P control\n'
                 'Same 36 seed/temperature/time-step combinations; 1 SE across four seeds per point',fontsize=12)
    for ext in ('png','svg'):fig.savefig(HERE/('boundary_control.'+ext),dpi=180)
    fig2,axs=plt.subplots(1,4,figsize=(12,3.5),layout='constrained')
    low=[('E_per_site',r'$E/N-E_{ED}/N$'),('D',r'$D-D_{ED}$'),
         ('S_Q',r'$S(Q)-S_{ED}(Q)$'),('SU2_Q',r'$S_\perp(Q)-2S^{zz}(Q)$')]
    for ax,(o,label) in zip(axs,low):
        ax.axhline(0,color='black',ls='--',lw=1)
        for x,(bc,root,color) in enumerate([('AP/P',APP,'#0072B2'),('P/P',HERE,'#D55E00')]):
            r=next(r for r in read(root/'dtau_extrapolated.tsv') if r['beta']=='16' and r['observable']==o)
            ax.errorbar([x],[float(r['mean'])-float(r['ed'])],yerr=[float(r['se'])],
                        fmt='o',color=color,capsize=5)
        ax.set_xticks([0,1],['AP/P','P/P']);ax.set_xlim(-.5,1.5)
        ax.set_ylabel(label);ax.grid(axis='y',alpha=.18)
    fig2.suptitle(r'Matched control at $\beta=16$, $\Delta\tau\to0$: residuals and SU(2)'
                  '\nError bars: 1 SE across four seeds per point, propagated through the fit',fontsize=12)
    for ext in ('png','svg'):fig2.savefig(HERE/('low_temperature_control.'+ext),dpi=180)


if __name__=='__main__':
    main()
