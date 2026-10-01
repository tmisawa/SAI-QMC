"""Independent HS enumeration and full Fock-space checks of local energy.

For a site i and its own HS flip, K_i' is obtained from the two affected
off-diagonal Green directions. Check the conditional formula against the
explicit flipped configuration, not a solver update.
"""
import json
from pathlib import Path
import numpy as np
from scipy.linalg import expm

HERE=Path(__file__).resolve().parent
cases=[]
for n,L,U,dt in [(2,4,8.,.125),(2,4,8.,.025),(4,2,8.,.25)]:
    hopping=np.zeros((n,n))
    for i in range(n-1):hopping[i,i+1]=hopping[i+1,i]=-1
    if n>2:hopping[0,-1]=hopping[-1,0]=-1
    ek=expm(-dt*hopping);lam=np.arccosh(np.exp(dt*U/2));count=1<<(n*L)
    weights=np.empty(count);kin=np.empty((count,L,n));double=np.empty_like(kin)
    predicted=np.empty_like(kin)
    for config in range(count):
        field=np.array([1 if config&(1<<j) else -1 for j in range(n*L)]).reshape(L,n)
        bs=[[ek@np.diag(np.exp(sigma*lam*field[l])) for l in range(L)] for sigma in [1,-1]]
        for cut in range(L):
            gs=[];weight=1.
            for spin in range(2):
                product=np.eye(n)
                for k in range(L):product=bs[spin][(cut+k)%L]@product
                matrix=np.eye(n)+product;weight*=np.linalg.det(matrix);gs.append(np.linalg.inv(matrix))
            if cut==0:weights[config]=weight
            else:assert np.isclose(weight,weights[config],rtol=1e-11)
            for i in range(n):
                gu,gd=gs;g=gu[i,i]
                kin[config,cut,i]=-sum(hopping[i,j]*(gu[j,i]+gd[j,i]) for j in range(n))
                double[config,cut,i]=(1-gu[i,i])*(1-gd[i,i])
                a=np.exp(-2*lam*field[cut,i]);ru=1+(a-1)*(1-g);rd=ru/a;r=ru*rd
                condk=-sum(hopping[i,j]*((1+ru)*gu[j,i]+(1+rd)*gu[i,j]) for j in range(n))/(1+r)
                predicted[config,cut,i]=condk+U*2*g*(1-g)/(1+r)
    energy=kin+U*double;direct=np.empty_like(energy)
    for config in range(count):
        for cut in range(L):
            for i in range(n):
                other=config^(1<<(cut*n+i));r=weights[other]/weights[config]
                direct[config,cut,i]=(energy[config,cut,i]+r*energy[other,cut,i])/(1+r)
    formula_error=float(np.max(np.abs(predicted-direct)))
    assert formula_error<1e-9
    probability=weights/weights.sum()
    ordinary=float(probability@energy.mean(axis=(1,2)))
    conditional=float(probability@predicted.mean(axis=(1,2)))
    dim=1<<(2*n);ann=[]
    for mode in range(2*n):
        c=np.zeros((dim,dim))
        for state in range(dim):
            if state&(1<<mode):c[state^(1<<mode),state]=(-1)**((state&((1<<mode)-1)).bit_count())
        ann.append(c)
    numbers=[a.T@a for a in ann];hkin=np.zeros((dim,dim))
    for spin in range(2):
        for i in range(n):
            for j in range(n):
                if hopping[i,j]:hkin+=hopping[i,j]*ann[spin*n+i].T@ann[spin*n+j]
    hint=U*sum(numbers[i]@numbers[n+i] for i in range(n))
    potential=hint-U/2*sum(numbers)
    transfer=np.linalg.matrix_power(expm(-dt*hkin)@expm(-dt*potential),L)
    z=np.trace(transfer);fock=float(np.trace(transfer@(hkin+hint))/z/n)
    fock_D=float(np.trace(transfer@hint)/z/n/U)
    ordinary_D=float(probability@double.mean(axis=(1,2)))
    assert abs(ordinary_D-fock_D)<1e-10
    assert abs(ordinary-fock)<1e-9 and abs(ordinary-conditional)<1e-10
    variance=np.sum(probability[:,None,None]*(energy-ordinary)**2,axis=0)
    condvar=np.sum(probability[:,None,None]*(predicted-conditional)**2,axis=0)
    assert np.all(condvar<=variance+1e-10)
    cases.append(dict(nsite=n,Ltr=L,U=U,dtau=dt,configurations=count,
        ordinary_E_per_site=ordinary,conditional_E_per_site=conditional,fock_trotter_E_per_site=fock,
        ordinary_D=ordinary_D,fock_trotter_D=fock_D,
        explicit_pair_formula_max_error=formula_error,
        local_variance=float(variance.mean()),conditional_local_variance=float(condvar.mean())))
result=dict(passed=True,cases=cases,
    scope='Local K_i+U D_i at the matching site/time cut, real half-filled bipartite PH path. Conditional single-coordinate variance is checked; integrated chain variance and cost require separate measurement.')
(HERE/'fock-reference.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
