#ifndef GREEN_H
#define GREEN_H

#include "field.h"
#include "linalg.h"
#include "model.h"

typedef struct Green Green;

typedef enum {
    GREEN_REBUILD_COMBINE = 0,
    GREEN_REBUILD_TWO_SIDED = 1,
    GREEN_REBUILD_CENTERED = 2
} GreenRebuildMode;

typedef int (*GreenScaleObserver)(void *ctx, const Green *G,
                                  const char *direction, int tau,
                                  int boundary, const UDV *factor);

struct Green {
    int n;
    int L;
    const Model *m;
    const Field *f;
    double *g;
    int cur_l;
    double sigma;
    double expv[2];
    double expvinv[2];
    double *B;
    double *Binv;
    double *tmp;
    double *u;
    double *v;
    /* Pending accepted local updates stored as G_current = g - delay_c *
       delay_v^T until the next flush/wrap/stabilized rebuild. */
    int delay_count;
    int delay_capacity;
    double *delay_c;
    double *delay_v;
    LinalgWork work;
    GreenScaleObserver scale_observer;
    void *scale_observer_ctx;
    const char *scale_observer_spin;
    GreenRebuildMode rebuild_mode;
    /* sign(det(1 + B_{L-1}...B_0)) from the last stabilized rebuild.
       Valid values are +1/-1; 0 is an "invalid/failed" sentinel. */
    int det_sign;
};

typedef struct {
    int n;
    int L;
    int stab;
    int M;
    int *b;
    /* S stores boundary products. The builder decides orientation:
       suffix: S[j] = B(L, b[j]) for j=1..M, with S[M]=I.
               S[0] is an unused identity sentinel.
       prefix: S[j] = B(b[j], 0) for j=0..M, with S[0]=I.
       Full end-of-sweep products are still rebuilt from the accumulated UDV
       in forward mode instead of consuming suffix S[0]. */
    UDV *S;
} GreenStack;

void green_alloc(Green *G, const Model *m, const Field *f, double sigma);
void green_free(Green *G);
void green_set_rebuild_mode(Green *G, GreenRebuildMode mode);
void green_build_B(const Green *G, int l, double *out);
void green_build_Binv(const Green *G, int l, double *out);
void green_build_Bblock(const Green *G, int l_begin, int len, double *out,
                        double *Btmp, double *tmp);
void green_stack_alloc(GreenStack *st, int n, int L, int stab);
void green_stack_free(GreenStack *st);
void green_stack_build_suffix(Green *G, GreenStack *st);
void green_stack_build_prefix(Green *G, GreenStack *st);
void green_stack_build(Green *G, GreenStack *st);
void green_stack_store(GreenStack *st, int j, const UDV *factor);
int green_from_scratch(Green *G, int l0);
/* Reconstructs from prefix * suffix into combined. combined must not alias
   prefix or suffix. cur_l may be L, which maps to 0. */
int green_from_boundary_factors(Green *G, const UDV *prefix,
                                const UDV *suffix, UDV *combined,
                                int cur_l);
/* Reconstructs from left * st->S[j] into combined. combined must not alias
   left or any GreenStack entry. */
int green_from_stack(Green *G, const GreenStack *st, const UDV *left,
                     UDV *combined, int j);
int green_from_left_udv(Green *G, const UDV *left);
void green_build_ph_down(const Green *up, const int *bipart, double *g_down);
double green_flipN(const Green *G, int i);
double green_ratio_N(const Green *G, int i, double N);
double green_ph_down_ratio_N(const Green *up, int i, double N);
double green_delay_ratio_N(const Green *G, int i, double N);
void green_delay_accept(Green *G, int i, double N);
void green_delay_flush(Green *G);
int green_delay_count(const Green *G);
void green_update(Green *G, int i, double N);
void green_wrap(Green *G);
void green_wrap_backward(Green *G);

/* log|det(1 + B_{L-1}...B_0)| and its sign for the current field, built from
   stab-slice block products (same blocks as green_stack_build_prefix).
   Does not modify G->g, G->cur_l, the delay buffer or G->det_sign. */
int green_logdet_full(Green *G, int stab, int *det_sign, double *logabs);

#endif
