#include "test_util.h"
#include "lattice.h"

int main(void)
{
    Lattice L;
    lattice_chain(&L, 4, -1.0, 1);
    CHECK(L.n == 4);
    CHECK(L.type == LAT_CHAIN);
    CHECK(L.Lx == 4);
    CHECK(L.Ly == 1);
    CHECK(L.has_coordinates == 1);
    for (int i = 0; i < 4; i++) {
        double s = 0.0;
        for (int j = 0; j < 4; j++) {
            if (j != i) {
                s += L.t[i + j * 4];
            }
        }
        CHECK_CLOSE(s, -2.0, 1e-12);
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            CHECK_CLOSE(L.t[i + j * 4], L.t[j + i * 4], 1e-12);
        }
    }
    CHECK(L.bipart[0] * L.bipart[1] == -1);
    CHECK(L.is_bipartite == 1);
    lattice_free(&L);
    CHECK(L.type == LAT_NONE);
    CHECK(L.Lx == 0);
    CHECK(L.Ly == 0);
    CHECK(L.has_coordinates == 0);

    Lattice c3;
    lattice_chain(&c3, 3, -1.0, 1);
    CHECK(c3.is_bipartite == 0);
    lattice_free(&c3);

    Lattice c4o;
    lattice_chain(&c4o, 3, -1.0, 0);
    CHECK(c4o.is_bipartite == 1);
    lattice_free(&c4o);

    Lattice c1;
    lattice_chain(&c1, 1, -1.0, 1);
    CHECK(c1.is_bipartite == 1);
    CHECK_CLOSE(c1.t[0], 0.0, 1e-12);
    lattice_free(&c1);

    Lattice s44;
    lattice_square(&s44, 4, 4, -1.0, 1);
    CHECK(s44.is_bipartite == 1);
    CHECK(s44.type == LAT_SQUARE);
    CHECK(s44.Lx == 4);
    CHECK(s44.Ly == 4);
    CHECK(s44.has_coordinates == 1);
    lattice_free(&s44);

    Lattice s34;
    lattice_square(&s34, 3, 4, -1.0, 1);
    CHECK(s34.is_bipartite == 0);
    lattice_free(&s34);

    Lattice S;
    lattice_square(&S, 2, 2, -1.0, 1);
    CHECK(S.n == 4);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            CHECK_CLOSE(S.t[i + j * 4], S.t[j + i * 4], 1e-12);
        }
    }
    lattice_free(&S);

    Lattice s1;
    lattice_square(&s1, 3, 1, -1.0, 1);
    CHECK(s1.Lx == 3);
    CHECK(s1.Ly == 1);
    for (int i = 0; i < 3; i++) {
        CHECK_CLOSE(s1.t[i + i * 3], 0.0, 1e-12);
    }
    lattice_free(&s1);

    Lattice s14;
    lattice_square(&s14, 1, 4, -1.0, 1);
    CHECK(s14.Lx == 1);
    CHECK(s14.Ly == 4);
    CHECK(s14.is_bipartite == 1);
    for (int i = 0; i < 4; i++) {
        CHECK_CLOSE(s14.t[i + i * 4], 0.0, 1e-12);
    }
    lattice_free(&s14);

    TEST_END();
}
