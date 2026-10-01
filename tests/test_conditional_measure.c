#include "test_util.h"
#include "conditional_measure.h"
#include "dqmc.h"
#include "lattice.h"
#include "replica.h"
#include "tempering.h"

#include <stdlib.h>
#include <string.h>

#include "conditional_reference.h"

static void exact_pairs(double U, double dt)
{
    Lattice lat; Model m;
    lattice_chain(&lat, 2, -1, 0);
    model_init(&m, &lat, U, dt, 1, 0);
    double weight[256], rawD[256][8], rawK[256][8], conD[256][8], conK[256][8];
    double Z=0;
    for (int c=0; c<256; c++) for (int cut=0; cut<4; cut++) {
        double gu[4], gd[4];
        const double wu=direct(c,cut,U,dt,1,gu), wd=direct(c,cut,U,dt,-1,gd);
        if (cut==0) { weight[c]=wu*wd; Z+=weight[c]; }
        for (int i=0; i<2; i++) {
            const int q=2*cut+i, other=c^(1<<q), j=1-i;
            double flipped[4], d, k;
            const double ru=direct(other,cut,U,dt,1,flipped)/wu;
            const double rd=direct(other,cut,U,dt,-1,flipped)/wd;
            Green g={0}; g.n=2; g.g=gu;
            CHECK(conditional_measure_local(&m,&g,i,ru,rd,&d,&k)==0);
            rawD[c][q]=(1-gu[i+2*i])*(1-gd[i+2*i]);
            rawK[c][q]=gu[j+2*i]+gd[j+2*i];
            conD[c][q]=d; conK[c][q]=k;
            /* Effective delayed matrix has exactly the same reference Green. */
            double base[4], cv[2]={.3,-.2}, vv[2]={-.1,.4};
            for (int x=0;x<2;x++) for(int y=0;y<2;y++)
                base[x+2*y]=gu[x+2*y]+cv[x]*vv[y];
            g.g=base; g.delay_count=1; g.delay_c=cv; g.delay_v=vv;
            CHECK(conditional_measure_local(&m,&g,i,ru,rd,&d,&k)==0);
            CHECK_CLOSE(d,conD[c][q],2e-14); CHECK_CLOSE(k,conK[c][q],2e-14);
        }
    }
    double dm=0, km=0, dc=0, kc=0;
    for (int c=0;c<256;c++) for (int q=0;q<8;q++) {
        const int other=c^(1<<q);
        const double r=weight[other]/weight[c];
        CHECK_CLOSE(conD[c][q],(rawD[c][q]+r*rawD[other][q])/(1+r),3e-13);
        CHECK_CLOSE(conK[c][q],(rawK[c][q]+r*rawK[other][q])/(1+r),3e-13);
        dm+=weight[c]*rawD[c][q]/(8*Z); dc+=weight[c]*conD[c][q]/(8*Z);
        km+=weight[c]*rawK[c][q]/(8*Z); kc+=weight[c]*conK[c][q]/(8*Z);
    }
    CHECK_CLOSE(dm,dc,2e-14); CHECK_CLOSE(km,kc,2e-14);
    /* Independent 16-state Fock transfer-matrix values for U=8, dt=1/8. */
    if (U==8 && dt==.125) {
        CHECK_CLOSE(dm,.06632707266956295,3e-14);
        CHECK_CLOSE(km+U*dm,.3704255788205761,5e-14);
    }
    model_free(&m); lattice_free(&lat);
}

typedef struct { Model m; Rng r; Field f; Dqmc d; } Chain;
static void init(Chain *c,const Lattice *l,double dt,DqmcSweepMode mode)
{
    model_init(&c->m,l,4,dt,1,0); rng_seed(&c->r,4242);
    field_init(&c->f,l->n,8,4,dt,&c->r);
    CHECK(dqmc_init_modes(&c->d,&c->m,&c->f,&c->r,2,NULL,mode,GREEN_REBUILD_CENTERED)==0);
}
static void release(Chain *c)
{ dqmc_free(&c->d); field_free(&c->f); model_free(&c->m); }
static void trajectories(DqmcSweepMode mode)
{
    Lattice l; lattice_chain(&l,4,-1,1);
    Chain a,b; init(&a,&l,.1,mode); init(&b,&l,.1,mode);
    CHECK(dqmc_enable_conditional_measure(&b.d,1)==0);
    for (int s=0;s<30;s++) {
        dqmc_sweep(&a.d); dqmc_sweep(&b.d);
        CHECK(a.d.status==0 && b.d.status==0);
        CHECK(b.d.conditional.count==32);
        if (s%3==0) {
            const ConditionalMeasure saved=b.d.conditional;
            CHECK(dqmc_global_site_pass(&a.d)==0);
            CHECK(dqmc_global_site_pass(&b.d)==0);
            CHECK(memcmp(&saved,&b.d.conditional,sizeof saved)==0);
        }
        CHECK(memcmp(a.f.s,b.f.s,32)==0);
        CHECK(memcmp(a.r.s,b.r.s,sizeof a.r.s)==0);
        CHECK(memcmp(a.d.Gu.g,b.d.Gu.g,16*sizeof(double))==0);
        CHECK(a.d.accept_accepted==b.d.accept_accepted);
    }
    /* Force a field exchange under identical weights; measurement state
       remains attached to the slot, including deliberately different sums. */
    CHECK(dqmc_enable_conditional_measure(&a.d,1)==0);
    a.d.conditional.sum_D=123; b.d.conditional.sum_D=456;
    ConditionalMeasure ca=a.d.conditional, cb=b.d.conditional;
    Dqmc *slots[2]={&a.d,&b.d}; TemperingLadder t;
    CHECK(tempering_ladder_init(&t,slots,2,123)==0);
    CHECK(tempering_ladder_round(&t)==0);
    CHECK(t.stats.accepted[0]==1);
    CHECK(memcmp(&ca,&a.d.conditional,sizeof ca)==0);
    CHECK(memcmp(&cb,&b.d.conditional,sizeof cb)==0);
    tempering_ladder_free(&t);
    CHECK(dqmc_enable_conditional_measure(&b.d,0)==0);
    dqmc_sweep(&b.d); CHECK(b.d.conditional.count==0);
    b.m.half_filling=0; CHECK(dqmc_enable_conditional_measure(&b.d,1)!=0);
    release(&a); release(&b); lattice_free(&l);
}

int main(void)
{
    exact_pairs(0,.125); exact_pairs(4,.1); exact_pairs(8,.125); exact_pairs(8,.025);
    trajectories(DQMC_SWEEP_FORWARD); trajectories(DQMC_SWEEP_ALTERNATING);
    /* Finite answer even when the unscaled numerator and weight overflow. */
    Model m={0}; Green g={0}; double matrix=1e200, d, k;
    m.n=g.n=1; m.half_filling=m.ph_symmetric=1; g.g=&matrix;
    CHECK(conditional_measure_local(&m,&g,0,-1e200,-.5e200,&d,&k)==0);
    CHECK_CLOSE(d,-4,1e-14); CHECK_CLOSE(k,0,0);
    CHECK(conditional_measure_local(&m,&g,0,NAN,1,&d,&k)!=0);
    CHECK(conditional_measure_local(&m,&g,0,-1,1,&d,&k)!=0);
    ReplicaBin bin={0};
    CHECK(replica_bin_add_conditional(&bin,.05,-8,8,16)==0);
    CHECK_CLOSE(bin.sum_Ehub_cond,-1.6,2e-15);
    const ReplicaBin old=bin;
    CHECK(replica_bin_add_conditional(&bin,NAN,0,8,16)!=0);
    CHECK(memcmp(&old,&bin,sizeof bin)==0);
    TEST_END();
}
