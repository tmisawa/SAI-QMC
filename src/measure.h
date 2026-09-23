#ifndef MEASURE_H
#define MEASURE_H

typedef struct {
    double E;
    double ekin;
    double eint;
    double ntot;
    double doublon;
} MeasSample;

MeasSample measure_sample(int n, const double *thop, double U,
                          const double *g_up, const double *g_dn);
int measure_sample_is_finite(const MeasSample *s);
void jackknife(const double *x, int N, double *mean, double *err);

#endif
