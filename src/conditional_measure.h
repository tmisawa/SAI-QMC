#ifndef CONDITIONAL_MEASURE_H
#define CONDITIONAL_MEASURE_H

#include "green.h"
#include "model.h"

/* Averages over the two values of the matching local HS variable, before
   its update. D is local double occupancy; K is the site's share of the
   hopping energy. The caller must use a real, half-filled PH model. */
int conditional_measure_local(const Model *m, const Green *g, int site,
                               double Ru, double Rd, double *D, double *K);

typedef struct {
    int enabled;
    unsigned long long count;
    double sum_D, sum_K;
    double lower_D, upper_D;
} ConditionalMeasure;

#endif
