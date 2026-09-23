#ifndef STRUCTURE_FACTOR_H
#define STRUCTURE_FACTOR_H

#include "lattice.h"

typedef struct {
    int enabled;
    int is_all;
    int n;
    int Lx;
    int Ly;
    int nq;
    int *mx;
    int *my;
    double *phase_cos;
} StructureFactorPlan;

typedef struct {
    int n;
    double *corr_disp;
} StructureFactorWorkspace;

typedef struct {
    double lhs;
    double rhs;
    double difference;
    double tolerance;
} StructureFactorSumRuleDiagnostics;

/* Backward-compatible Szz aliases and wrappers are retained for existing
 * callers; new shared code may use the structure_factor_* names directly. */
typedef StructureFactorPlan SzzPlan;
typedef StructureFactorWorkspace SzzWorkspace;
typedef StructureFactorSumRuleDiagnostics SzzSumRuleDiagnostics;

int structure_factor_plan_init(StructureFactorPlan *plan, const Lattice *L,
                               const char *selector);
void structure_factor_plan_free(StructureFactorPlan *plan);

int structure_factor_workspace_init(StructureFactorWorkspace *work,
                                    const StructureFactorPlan *plan);
void structure_factor_workspace_free(StructureFactorWorkspace *work);

int szz_plan_init(SzzPlan *plan, const Lattice *L, const char *selector);
void szz_plan_free(SzzPlan *plan);

int szz_workspace_init(SzzWorkspace *work, const SzzPlan *plan);
void szz_workspace_free(SzzWorkspace *work);

int measure_szz_sample(const SzzPlan *plan, SzzWorkspace *work,
                       const double *g_up, const double *g_dn, double *out);
int measure_sperp_sample(const StructureFactorPlan *plan,
                         StructureFactorWorkspace *work,
                         const double *g_up, const double *g_dn, double *out);
int measure_szz_sperp_sample(
    const StructureFactorPlan *szz_plan, StructureFactorWorkspace *szz_work,
    double *szz_out, const StructureFactorPlan *sperp_plan,
    StructureFactorWorkspace *sperp_work, double *sperp_out,
    const double *g_up, const double *g_dn);

int spin_pair_correlations(int n, const double *g_up, const double *g_dn,
                           int i, int j, double *czz, double *cperp);

int szz_bin_ratio(const double *num, int nq, double sum_sign, double *out);
int sperp_bin_ratio(const double *num, int nq, double sum_sign, double *out);
int szz_sum_rule_check(const SzzPlan *plan, const double *szz, double ntot,
                       double doublon, SzzSumRuleDiagnostics *diagnostics);
int sperp_sum_rule_check(
    const StructureFactorPlan *plan, const double *sperp, double ntot,
    double doublon, StructureFactorSumRuleDiagnostics *diagnostics);

#endif
