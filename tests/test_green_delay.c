#include "test_util.h"
#include "field.h"
#include "green.h"
#include "lattice.h"
#include "model.h"

static void check_close_mat(int n, const double *a, const double *b,
                            double tol)
{
    for (int i = 0; i < n * n; i++) {
        CHECK_CLOSE(a[i], b[i], tol);
    }
}

static void check_pending_ratios(Green *delayed, Green *ref)
{
    const int n = delayed->n;
    for (int i = 0; i < n; i++) {
        const double Nd = green_flipN(delayed, i);
        const double Nr = green_flipN(ref, i);
        CHECK_CLOSE(Nd, Nr, 0.0);
        CHECK_CLOSE(green_delay_ratio_N(delayed, i, Nd),
                    green_ratio_N(ref, i, Nr), 1e-11);
    }
}

static void apply_one_delayed(Green *delayed, Green *ref, Field *fd,
                              Field *fr, int site)
{
    const int n = delayed->n;
    const int off = delayed->cur_l * n + site;
    const double Nd = green_flipN(delayed, site);
    const double Nr = green_flipN(ref, site);
    CHECK_CLOSE(Nd, Nr, 0.0);
    CHECK_CLOSE(green_delay_ratio_N(delayed, site, Nd),
                green_ratio_N(ref, site, Nr), 1e-11);

    green_delay_accept(delayed, site, Nd);
    green_update(ref, site, Nr);
    fd->s[off] *= -1;
    fr->s[off] *= -1;
    check_pending_ratios(delayed, ref);
}

static void run_sequence_case(double sigma)
{
    const int Lx = 6;
    const int Ltr = 5;
    const double dtau = 0.1;
    const double U = 4.0;
    static const int seq[] = {2, 0, 5, 1, 4, 3, 2, 2, 5, 0, 1, 3,
                              4, 4, 0, 5, 2, 1, 3, 0, 4, 5, 1};

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, U, dtau, 1, 0.0);

    Rng rd;
    Rng rr;
    rng_seed(&rd, 71);
    rng_seed(&rr, 71);
    Field fd;
    Field fr;
    field_init(&fd, Lx, Ltr, U, dtau, &rd);
    field_init(&fr, Lx, Ltr, U, dtau, &rr);

    Green delayed;
    Green ref;
    green_alloc(&delayed, &m, &fd, sigma);
    green_alloc(&ref, &m, &fr, sigma);
    CHECK(green_from_scratch(&delayed, 0) == 0);
    CHECK(green_from_scratch(&ref, 0) == 0);

    for (int p = 0; p < (int)(sizeof(seq) / sizeof(seq[0])); p++) {
        apply_one_delayed(&delayed, &ref, &fd, &fr, seq[p]);
    }
    CHECK(green_delay_count(&delayed) > 0);
    green_delay_flush(&delayed);
    CHECK(green_delay_count(&delayed) == 0);
    check_close_mat(Lx, delayed.g, ref.g, 1e-10);

    green_free(&ref);
    green_free(&delayed);
    field_free(&fr);
    field_free(&fd);
    model_free(&m);
    lattice_free(&L);
}

static void run_wrap_flush_case(double sigma)
{
    const int Lx = 4;
    const int Ltr = 4;
    static const int seq[] = {1, 3, 0, 2, 1};

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, 6.0, 0.1, 1, 0.0);
    Rng rd;
    Rng rr;
    rng_seed(&rd, 82);
    rng_seed(&rr, 82);
    Field fd;
    Field fr;
    field_init(&fd, Lx, Ltr, 6.0, 0.1, &rd);
    field_init(&fr, Lx, Ltr, 6.0, 0.1, &rr);

    Green delayed;
    Green ref;
    green_alloc(&delayed, &m, &fd, sigma);
    green_alloc(&ref, &m, &fr, sigma);
    CHECK(green_from_scratch(&delayed, 0) == 0);
    CHECK(green_from_scratch(&ref, 0) == 0);

    for (int p = 0; p < (int)(sizeof(seq) / sizeof(seq[0])); p++) {
        apply_one_delayed(&delayed, &ref, &fd, &fr, seq[p]);
    }
    CHECK(green_delay_count(&delayed) > 0);
    green_wrap(&delayed);
    green_wrap(&ref);
    CHECK(green_delay_count(&delayed) == 0);
    CHECK(delayed.cur_l == ref.cur_l);
    check_close_mat(Lx, delayed.g, ref.g, 1e-10);

    green_free(&ref);
    green_free(&delayed);
    field_free(&fr);
    field_free(&fd);
    model_free(&m);
    lattice_free(&L);
}

static void run_rebuild_clears_case(double sigma)
{
    const int Lx = 4;
    const int Ltr = 6;

    Lattice L;
    lattice_chain(&L, Lx, -1.0, 1);
    Model m;
    model_init(&m, &L, 4.0, 0.1, 1, 0.0);
    Rng rd;
    Rng rr;
    rng_seed(&rd, 93);
    rng_seed(&rr, 93);
    Field fd;
    Field fr;
    field_init(&fd, Lx, Ltr, 4.0, 0.1, &rd);
    field_init(&fr, Lx, Ltr, 4.0, 0.1, &rr);

    Green delayed;
    Green ref;
    green_alloc(&delayed, &m, &fd, sigma);
    green_alloc(&ref, &m, &fr, sigma);
    CHECK(green_from_scratch(&delayed, 0) == 0);
    CHECK(green_from_scratch(&ref, 0) == 0);

    apply_one_delayed(&delayed, &ref, &fd, &fr, 2);
    CHECK(green_delay_count(&delayed) > 0);
    CHECK(green_from_scratch(&delayed, 0) == 0);
    CHECK(green_from_scratch(&ref, 0) == 0);
    CHECK(green_delay_count(&delayed) == 0);
    check_close_mat(Lx, delayed.g, ref.g, 1e-10);

    green_free(&ref);
    green_free(&delayed);
    field_free(&fr);
    field_free(&fd);
    model_free(&m);
    lattice_free(&L);
}

int main(void)
{
    run_sequence_case(1.0);
    run_sequence_case(-1.0);
    run_wrap_flush_case(1.0);
    run_wrap_flush_case(-1.0);
    run_rebuild_clears_case(1.0);
    run_rebuild_clears_case(-1.0);
    TEST_END();
}
