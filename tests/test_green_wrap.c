#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "model.h"

static void check_close_mat(int n, const double *a, const double *b)
{
    for (int k = 0; k < n * n; k++) {
        CHECK_CLOSE(a[k], b[k], 1e-9);
    }
}

int main(void)
{
    const int Lx = 4;
    const int Ltr = 8;
    const double dtau = 0.1;
    const double U = 4.0;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);
    Rng r;
    rng_seed(&r, 11);
    Field f;
    field_init(&f, Lx, Ltr, U, dtau, &r);

    Green G;
    Green G2;
    Green Ref;
    green_alloc(&G, &m, &f, 1.0);
    green_alloc(&G2, &m, &f, 1.0);
    green_alloc(&Ref, &m, &f, 1.0);

    for (int tau = 0; tau < Ltr; tau++) {
        CHECK(green_from_scratch(&G, tau) == 0);
        green_wrap(&G);
        CHECK(G.cur_l == (tau + 1) % Ltr);
        CHECK(green_from_scratch(&Ref, (tau + 1) % Ltr) == 0);
        check_close_mat(Lx, G.g, Ref.g);

        CHECK(green_from_scratch(&G, tau) == 0);
        green_wrap_backward(&G);
        CHECK(G.cur_l == (tau - 1 + Ltr) % Ltr);
        CHECK(green_from_scratch(&Ref, (tau - 1 + Ltr) % Ltr) == 0);
        check_close_mat(Lx, G.g, Ref.g);

        CHECK(green_from_scratch(&G, tau) == 0);
        CHECK(green_from_scratch(&G2, tau) == 0);
        green_wrap(&G);
        green_wrap_backward(&G);
        CHECK(G.cur_l == tau);
        check_close_mat(Lx, G.g, G2.g);
    }

    CHECK(green_from_scratch(&G, 3) == 0);
    green_delay_accept(&G, 1, green_flipN(&G, 1));
    CHECK(green_delay_count(&G) > 0);
    green_wrap_backward(&G);
    CHECK(G.cur_l == 2);
    CHECK(green_delay_count(&G) == 0);

    green_free(&Ref);
    green_free(&G2);
    green_free(&G);
    field_free(&f);
    model_free(&m);
    lattice_free(&L);
    TEST_END();
}
