#include "test_util.h"
#include "io.h"

#include <stdio.h>
#include <string.h>

static int read_text(const char *text, Params *p)
{
    const char *path = "tests/tmp_io_bc.in";
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        return -1;
    }
    fprintf(fp, "%snmeas=20\nnbin=2\n", text);
    fclose(fp);
    const int rc = params_read(p, path);
    remove(path);
    return rc;
}

/* Reads text, expects success, and checks the resolved boundary and label. */
static void expect(const char *text, LatBoundary bx, LatBoundary by,
                   int legacy, const char *label)
{
    Params p;
    const int rc = read_text(text, &p);
    CHECK(rc == 0);
    if (rc != 0) {
        printf("  input was:\n%s", text);
        return;
    }
    CHECK(p.bc_x == bx && p.bc_y == by);
    CHECK(params_boundary_is_legacy(&p) == legacy);
    char got[64];
    CHECK(params_boundary_label(&p, got, sizeof got) == 0);
    if (strcmp(got, label) != 0) {
        printf("FAIL label: got \"%s\" want \"%s\"\n", got, label);
        g_fail++;
    }
}

static void reject(const char *text)
{
    Params p;
    if (read_text(text, &p) == 0) {
        printf("FAIL accepted:\n%s", text);
        g_fail++;
    }
}

int main(void)
{
    Params p;

    /* defaults and the legacy keys */
    CHECK(read_text("lattice=chain\nLx=4\n", &p) == 0);
    CHECK(p.pbc == 1 && p.pbc_given == 0 && p.bc_x_given == 0 &&
          p.bc_y_given == 0);
    expect("lattice=chain\nLx=4\n", LAT_BC_PERIODIC, LAT_BC_PERIODIC, 1,
           "pbc=1");
    expect("lattice=square\nLx=4\nLy=4\npbc=0\n", LAT_BC_OPEN, LAT_BC_OPEN, 1,
           "pbc=0");
    CHECK(read_text("lattice=square\nLx=4\nLy=4\npbc=0\n", &p) == 0);
    CHECK(p.pbc_given == 1);
    expect("lattice=square\nLx=4\nLy=4\nbc=open\n", LAT_BC_OPEN, LAT_BC_OPEN,
           1, "pbc=0");
    expect("lattice=square\nLx=4\nLy=4\nbc=periodic\n", LAT_BC_PERIODIC,
           LAT_BC_PERIODIC, 1, "pbc=1");

    /* directional keys */
    expect("lattice=square\nLx=4\nLy=4\nbc_x=antiperiodic\n",
           LAT_BC_ANTIPERIODIC, LAT_BC_PERIODIC, 0,
           "bc_x=antiperiodic bc_y=periodic");
    CHECK(read_text("lattice=square\nLx=4\nLy=4\nbc_x=antiperiodic\n", &p) ==
          0);
    CHECK(p.bc_x_given == 1 && p.bc_y_given == 0 && p.pbc_given == 0);
    expect("lattice=square\nLx=4\nLy=4\nbc_y=antiperiodic\n", LAT_BC_PERIODIC,
           LAT_BC_ANTIPERIODIC, 0, "bc_x=periodic bc_y=antiperiodic");
    expect("lattice=square\nLx=4\nLy=4\nbc_x=antiperiodic\nbc_y=open\n",
           LAT_BC_ANTIPERIODIC, LAT_BC_OPEN, 0,
           "bc_x=antiperiodic bc_y=open");
    expect("lattice=square\nLx=4\nLy=4\nbc_x=periodic\nbc_y=open\n",
           LAT_BC_PERIODIC, LAT_BC_OPEN, 0, "bc_x=periodic bc_y=open");
    expect("lattice=square\nLx=4\nLy=4\nbc_x=open\n", LAT_BC_OPEN,
           LAT_BC_PERIODIC, 0, "bc_x=open bc_y=periodic");
    expect("lattice=square\nLx=3\nLy=4\nbc_x=open\nbc_y=antiperiodic\n",
           LAT_BC_OPEN, LAT_BC_ANTIPERIODIC, 0, "bc_x=open bc_y=antiperiodic");
    expect("lattice=square\nLx=4\nLy=4\nbc_x=antiperiodic\n"
           "bc_y=antiperiodic\n",
           LAT_BC_ANTIPERIODIC, LAT_BC_ANTIPERIODIC, 0,
           "bc_x=antiperiodic bc_y=antiperiodic");
    expect("lattice=square\nLx=4\nLy=4\nbc_x=periodic\nbc_y=periodic\n",
           LAT_BC_PERIODIC, LAT_BC_PERIODIC, 1, "pbc=1");
    expect("lattice=square\nLx=4\nLy=4\nbc_x=open\nbc_y=open\n", LAT_BC_OPEN,
           LAT_BC_OPEN, 1, "pbc=0");
    expect("lattice=chain\nLx=4\nbc_x=antiperiodic\n", LAT_BC_ANTIPERIODIC,
           LAT_BC_PERIODIC, 0, "bc_x=antiperiodic");
    expect("lattice=chain\nLx=6\nbc_x=antiperiodic\n", LAT_BC_ANTIPERIODIC,
           LAT_BC_PERIODIC, 0, "bc_x=antiperiodic");
    expect("lattice=chain\nLx=4\nbc_x=open\n", LAT_BC_OPEN, LAT_BC_PERIODIC, 1,
           "pbc=0");
    expect("lattice=chain\nLx=4\nbc_x=open\nbc_x=antiperiodic\n",
           LAT_BC_ANTIPERIODIC, LAT_BC_PERIODIC, 0, "bc_x=antiperiodic");

    /* lattice=file keeps the legacy label from the pbc value */
    expect("lattice=file\nlatfile=hop.txt\n", LAT_BC_PERIODIC, LAT_BC_PERIODIC,
           1, "pbc=1");
    expect("lattice=file\nlatfile=hop.txt\npbc=0\n", LAT_BC_OPEN, LAT_BC_OPEN,
           1, "pbc=0");

    /* rejected inputs (spec 3.4 order: values, combination, lattice, length) */
    reject("lattice=square\nLx=4\nLy=4\nbc_x=Periodic\n");
    reject("lattice=square\nLx=4\nLy=4\nbc_y=apbc\n");
    reject("lattice=square\nLx=4\nLy=4\nbc_x=anti periodic\n");
    reject("lattice=square\nLx=4\nLy=4\nbc_x=\n");
    reject("lattice=square\nLx=4\nLy=4\nbc=antiperiodic\n");
    reject("lattice=square\nLx=4\nLy=4\npbc=1\nbc_x=antiperiodic\n");
    reject("lattice=square\nLx=4\nLy=4\nbc=periodic\nbc_y=open\n");
    reject("lattice=square\nLx=4\nLy=4\npbc=0\nbc_y=periodic\n");
    reject("lattice=file\nlatfile=hop.txt\nbc_x=periodic\n");
    reject("lattice=chain\nLx=4\nbc_y=periodic\n");
    reject("lattice=square\nLx=2\nLy=4\nbc_x=antiperiodic\n");
    reject("lattice=square\nLx=3\nLy=4\nbc_x=antiperiodic\n");
    reject("lattice=square\nLx=6\nLy=1\nbc_y=antiperiodic\n");
    reject("lattice=square\nLx=4\nLy=2\nbc_y=antiperiodic\n");
    reject("lattice=chain\nLx=2\nbc_x=antiperiodic\n");
    reject("lattice=chain\nLx=5\nbc_x=antiperiodic\n");

    /* label buffer size */
    CHECK(read_text("lattice=square\nLx=4\nLy=4\nbc_x=antiperiodic\n", &p) ==
          0);
    char small[8];
    CHECK(params_boundary_label(&p, small, sizeof small) == 1);
    CHECK(read_text("lattice=chain\nLx=4\n", &p) == 0);
    char six[6];
    CHECK(params_boundary_label(&p, six, sizeof six) == 0);
    CHECK(strcmp(six, "pbc=1") == 0);
    CHECK(params_boundary_label(&p, six, 5) == 1);
    TEST_END();
}
