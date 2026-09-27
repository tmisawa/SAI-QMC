#ifndef TEMPERING_OUT_H
#define TEMPERING_OUT_H

#include <stdio.h>

#include "io.h"
#include "tempering_run.h"

/* Writes the tempering_file (TSV): header comments, then pair/slot/walker/
   ladder/cost rows for each of the nladder ladders in outs[]. Integer
   columns are decimal integers; only cost rows carry (worker) seconds.
   Returns nonzero on any I/O error or inconsistent input. */
int tempering_out_write(FILE *fp, const Params *p, const TemperingLadderOut *outs,
                        int nladder);

/* CLOCK_MONOTONIC seconds, for solver_elapsed_seconds. */
double tempering_monotonic_seconds(void);

#endif
