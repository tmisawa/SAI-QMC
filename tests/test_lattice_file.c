#include "test_util.h"
#include "lattice.h"

#include <stdio.h>

int main(void)
{
    Lattice L;
    lattice_chain(&L, 4, -1.0, 1);
    const char *path = "/tmp/aftest_hop.txt";
    CHECK(lattice_dump(&L, path) == 0);

    Lattice F;
    CHECK(lattice_from_file(&F, path) == 0);
    CHECK(F.n == 4);
    CHECK(F.type == LAT_FILE);
    CHECK(F.Lx == 0);
    CHECK(F.Ly == 0);
    CHECK(F.has_coordinates == 0);
    for (int k = 0; k < 16; k++) {
        CHECK_CLOSE(F.t[k], L.t[k], 1e-12);
    }
    CHECK(F.is_bipartite == 1);
    lattice_free(&F);
    lattice_free(&L);

    const char *bad_diag = "/tmp/aftest_diag_hop.txt";
    FILE *fp = fopen(bad_diag, "w");
    fprintf(fp, "2\n0.5 -1\n-1 0\n");
    fclose(fp);
    Lattice B;
    CHECK(lattice_from_file(&B, bad_diag) != 0);

    const char *bad_asym = "/tmp/aftest_asym_hop.txt";
    fp = fopen(bad_asym, "w");
    fprintf(fp, "2\n0 -1\n-0.5 0\n");
    fclose(fp);
    CHECK(lattice_from_file(&B, bad_asym) != 0);

    const char *tri = "/tmp/aftest_tri_hop.txt";
    fp = fopen(tri, "w");
    fprintf(fp, "3\n0 -1 -1\n-1 0 -1\n-1 -1 0\n");
    fclose(fp);
    CHECK(lattice_from_file(&B, tri) == 0);
    CHECK(B.is_bipartite == 0);
    lattice_free(&B);

    TEST_END();
}
