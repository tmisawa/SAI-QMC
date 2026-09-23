#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "linalg.h"
#include "model.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    const int Lx = 4;
    const int Ltr = 20;
    const double dtau = 0.1;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, 0.0, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 7);
    Field f;
    field_init(&f, Lx, Ltr, 0.0, dtau, &r);
    Green G;
    green_alloc(&G, &m, &f, 1.0);
    green_from_scratch(&G, 0);

    const double beta = Ltr * dtau;
    double *eK = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    double *IpB = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    double *gref = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    la_expm_sym(Lx, m.K, -beta, eK);
    for (int i = 0; i < Lx * Lx; i++) {
        IpB[i] = eK[i];
    }
    for (int i = 0; i < Lx; i++) {
        IpB[i + i * Lx] += 1.0;
    }
    CHECK(la_inverse(Lx, IpB, gref) == 0);
    for (int i = 0; i < Lx * Lx; i++) {
        CHECK_CLOSE(G.g[i], gref[i], 1e-9);
    }
    free(eK);
    free(IpB);
    free(gref);
    green_free(&G);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);

    const int Ltr2 = 5;
    const double U = 4.0;
    Lattice L2;
    lattice_chain(&L2, Lx, -1.0, 1);
    Model m2;
    model_init(&m2, &L2, U, dtau, 1, 0.0);
    Rng r2;
    rng_seed(&r2, 13);
    Field f2;
    field_init(&f2, Lx, Ltr2, U, dtau, &r2);
    Green G2;
    green_alloc(&G2, &m2, &f2, 1.0);

    double *B = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    double *P = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    double *tmp = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    double *Ip = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    double *g2ref = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
    for (int l0 = 0; l0 < 2; l0++) {
        green_from_scratch(&G2, l0);
        la_eye(Lx, P);
        for (int k = 0; k < Ltr2; k++) {
            const int l = (l0 + k) % Ltr2;
            green_build_B(&G2, l, B);
            la_matmul(Lx, B, P, tmp);
            memcpy(P, tmp, sizeof(double) * (size_t)Lx * (size_t)Lx);
        }
        for (int i = 0; i < Lx * Lx; i++) {
            Ip[i] = P[i];
        }
        for (int i = 0; i < Lx; i++) {
            Ip[i + i * Lx] += 1.0;
        }
        CHECK(la_inverse(Lx, Ip, g2ref) == 0);
        for (int i = 0; i < Lx * Lx; i++) {
            CHECK_CLOSE(G2.g[i], g2ref[i], 1e-9);
        }

        /* det_sign must match the brute-force sign(det(I + B_{L-1}...B_0)). */
        double *Ipc = malloc(sizeof(double) * (size_t)Lx * (size_t)Lx);
        memcpy(Ipc, Ip, sizeof(double) * (size_t)Lx * (size_t)Lx);
        int sgn_ref = 0;
        double lad_ref = 0.0;
        CHECK(la_logdet(Lx, Ipc, &sgn_ref, &lad_ref) == 0);
        CHECK(G2.det_sign == sgn_ref);
        CHECK(G2.det_sign == 1 || G2.det_sign == -1);
        free(Ipc);
    }
    free(B);
    free(P);
    free(tmp);
    free(Ip);
    free(g2ref);
    green_free(&G2);
    field_free(&f2);
    model_free(&m2);
    lattice_free(&L2);

    TEST_END();
}
