/* Independent-series check against full HS enumeration, using the integrated
   fixed-beta and PT drivers. Deliberately separate from fast deterministic
   checks: this is a statistical regression, not proof of general mixing. */
#include "test_util.h"
#include "conditional_reference.h"
#include "replica_run.h"
#include "tempering_run.h"
#include <stdlib.h>

enum { REPS=64, SLOTS=3 };

static void exact(double dt, double *energy, double *doublon)
{
    double z=0, se=0, sd=0;
    for (int c=0;c<256;c++) {
        double gu[4],gd[4];
        const double w=direct(c,0,8,dt,1,gu)*direct(c,0,8,dt,-1,gd);
        const double d=((1-gu[0])*(1-gd[0])+(1-gu[3])*(1-gd[3]))/2;
        const double k=gu[1]+gu[2]+gd[1]+gd[2];
        z+=w; sd+=w*d; se+=w*(k+16*d);
    }
    *energy=se/z; *doublon=sd/z;
}

static void check_mean(const double *v,double ref,const char *quantity,int slot)
{
    double mean=0,variance=0;
    for(int r=0;r<REPS;r++) mean+=v[r]/REPS;
    for(int r=0;r<REPS;r++) variance+=(v[r]-mean)*(v[r]-mean);
    const double se=sqrt(variance/(REPS-1)/REPS);
    const double z=(mean-ref)/se;
    printf("  slot=%d %s mean=%.12g SE=%.5g exact=%.12g z=%+.3f\n",slot,quantity,mean,se,ref,z);
    CHECK(isfinite(z) && fabs(z)<5.0);
}

static void run_case(int pt,int alternating)
{
    Params p={0};
    strcpy(p.lattice,"chain"); p.Lx=2; p.Ly=1; p.U=8; p.dtau=.125;
    p.beta_list[0]=pt?.25:.5; p.beta_list[1]=.375; p.beta_list[2]=.5;
    p.nbeta=pt?3:1; p.nwarm=200; p.nmeas=3000; p.nbin=3; p.stab_interval=2;
    p.seed=2026093017ULL; p.nrep=REPS; p.conditional_measure=1;
    strcpy(p.sweep_order,alternating?"alternating":"forward");
    strcpy(p.green_rebuild,"centered");
    strcpy(p.global_update,alternating?"site":"none"); p.global_interval=3;
    strcpy(p.tempering,pt?"dtau_ladder":"none");
    p.tempering_ltr=4; p.tempering_interval=1;
    Lattice l; lattice_chain(&l,2,-1,0);
    StructureFactorPlan off={0};
    double es[SLOTS][REPS],ds[SLOTS][REPS];
    printf("conditional sampling: pt=%d alternating/global=%d\n",pt,alternating);
    for(int r=0;r<REPS;r++) {
        ReplicaResult result[SLOTS]={{0}};
        if(pt) {
            TemperingLadderOut out;
            CHECK(tempering_ladder_out_alloc(&out,3,p.nbin)==0);
            CHECK(dqmc_run_ladder(&p,&l,4,r,&off,&off,result,&out)==0);
            CHECK(!out.failed);
            tempering_ladder_out_free(&out);
        } else {
            CHECK(dqmc_run_replica(&p,&l,0,4,4,r,replica_seed(p.seed,0,r),
                                   &off,&off,NULL,result)==0);
        }
        for(int k=0;k<p.nbeta;k++) {
            double e=0,d=0; int count=0;
            CHECK(result[k].status==0);
            if(result[k].bins==NULL) exit(1);
            for(int b=0;b<p.nbin;b++) {
                const ReplicaBin *bin=&result[k].bins[b];
                CHECK(bin->conditional_count==bin->count);
                count+=bin->conditional_count; e+=bin->sum_Ehub_cond; d+=bin->sum_D_cond;
            }
            CHECK(count==p.nmeas); es[k][r]=e/count; ds[k][r]=d/count;
            replica_result_free(&result[k]);
        }
    }
    for(int k=0;k<p.nbeta;k++) {
        double e,d; exact(p.beta_list[k]/4,&e,&d);
        check_mean(es[k],e,"E",k); check_mean(ds[k],d,"D",k);
    }
    lattice_free(&l);
}

int main(void)
{
    run_case(0,0); run_case(0,1); run_case(1,0); run_case(1,1);
    TEST_END();
}
