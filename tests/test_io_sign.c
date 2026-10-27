/* mu, tprime and sign_file inputs (plan T1/T5): resolution of mu without
   rounding, the half-filling candidate, the extended-schema rule, rejected
   combinations, and the propagation of mu into the replica model. */
#include "test_util.h"
#include "io.h"
#include "lattice.h"
#include "model.h"
#include "replica_chain.h"

#include <stdio.h>
#include <string.h>

static int read_with(const char *base, const char *extra, Params *p)
{
    const char *path = "tests/tmp_io_sign.in";
    FILE *fp = fopen(path, "w");
    fprintf(fp, "%sdtau=0.1\nbeta_list=1\nnwarm=10\nnmeas=20\nnbin=2\n"
                "stab=4\nseed=1\n%s", base, extra);
    fclose(fp);
    const int rc = params_read(p, path);
    remove(path);
    return rc;
}

static const char *chain4 = "lattice=chain\nLx=4\npbc=1\nt=-1.0\nU=4\n";
static const char *square44 = "lattice=square\nLx=4\nLy=4\nt=-1.0\nU=4\n";

static void check_mu(void)
{
    Params p;
    CHECK(read_with(chain4, "", &p) == 0);
    CHECK(p.mu_given == 0 && p.mu == 2.0 && params_mu_is_half(&p));
    CHECK(p.tprime_given == 0 && p.tprime == 0.0);
    CHECK(strcmp(p.sign_file, "none") == 0 && !params_sign_output_enabled(&p));
    CHECK(p.sign_file_given == 0 && !params_new_keys(&p));
    CHECK(params_extended_schema(&p, 1) == 0);
    CHECK(params_extended_schema(&p, 0) == 1); /* any non-PH model */

    /* mu = U/2 given explicitly: half-filling candidate, extended schema */
    CHECK(read_with(chain4, "mu=2\n", &p) == 0);
    CHECK(p.mu_given == 1 && p.mu == 2.0 && params_mu_is_half(&p));
    CHECK(params_extended_schema(&p, 1) == 1);
    /* U set after mu: mu stays as given, U/2 is the resolved default only */
    CHECK(read_with("lattice=chain\nLx=4\nmu=1.5\nU=3\n", "", &p) == 0);
    CHECK(p.mu == 1.5 && params_mu_is_half(&p));
    CHECK(read_with("lattice=chain\nLx=4\nU=6\n", "", &p) == 0);
    CHECK(p.mu == 3.0 && !p.mu_given);

    /* no tolerance: a value one ulp away is not half filling */
    CHECK(read_with(chain4, "mu=2.0000000000000004\n", &p) == 0);
    CHECK(p.mu != 2.0 && !params_mu_is_half(&p));
    CHECK(read_with(chain4, "mu=-0.75\n", &p) == 0);
    CHECK(p.mu == -0.75 && !params_mu_is_half(&p));

    CHECK(read_with(chain4, "mu=nan\n", &p) != 0);
    CHECK(read_with(chain4, "mu=inf\n", &p) != 0);
    CHECK(read_with(chain4, "mu=1e999\n", &p) != 0);
    CHECK(read_with(chain4, "mu=abc\n", &p) != 0);
}

static void check_tprime(void)
{
    Params p;
    CHECK(read_with(square44, "tprime=-0.2\n", &p) == 0);
    CHECK(p.tprime_given == 1 && p.tprime == -0.2);
    CHECK(params_extended_schema(&p, 1) == 1);
    CHECK(read_with(square44, "tprime=0\n", &p) == 0);
    CHECK(p.tprime_given == 1 && p.tprime == 0.0);
    /* tprime is a square-lattice key, even when it is 0 */
    CHECK(read_with(chain4, "tprime=0\n", &p) != 0);
    CHECK(read_with(chain4, "tprime=-0.2\n", &p) != 0);
    CHECK(read_with("lattice=file\nlatfile=x.txt\nU=4\n", "tprime=0.1\n", &p) != 0);
    /* a nonzero tprime needs both lengths >= 2 */
    CHECK(read_with("lattice=square\nLx=4\nLy=1\nU=4\n", "tprime=-0.2\n", &p) != 0);
    CHECK(read_with("lattice=square\nLx=1\nLy=4\nU=4\n", "tprime=-0.2\n", &p) != 0);
    CHECK(read_with("lattice=square\nLx=4\nLy=1\nU=4\n", "tprime=0\n", &p) == 0);
    CHECK(read_with(square44, "tprime=nan\n", &p) != 0);
}

static void check_sign_file(void)
{
    Params p;
    CHECK(read_with(chain4, "sign_file=sign.dat\n", &p) == 0);
    CHECK(strcmp(p.sign_file, "sign.dat") == 0 && params_sign_output_enabled(&p));
    CHECK(p.sign_file_given == 1 && params_new_keys(&p));
    CHECK(params_extended_schema(&p, 1) == 1);
    CHECK(read_with(chain4, "sign_file=none\n", &p) == 0);
    CHECK(p.sign_file_given == 1 && params_new_keys(&p));
    CHECK(!params_sign_output_enabled(&p) && params_extended_schema(&p, 1) == 1);
    /* Reusing a Params object must not keep a previous explicit setting. */
    CHECK(read_with(chain4, "", &p) == 0);
    CHECK(!p.sign_file_given && !params_new_keys(&p));
    CHECK(!params_sign_output_enabled(&p) && !params_extended_schema(&p, 1));
    /* strict string: no spaces, no empty value */
    CHECK(read_with(chain4, "sign_file=a b\n", &p) != 0);
    CHECK(read_with(chain4, "sign_file=\n", &p) != 0);
}

/* An odd periodic chain without any new key is non-PH and extended. */
static void check_odd_chain(void)
{
    Params p;
    CHECK(read_with("lattice=chain\nLx=5\npbc=1\nt=-1.0\nU=4\n", "", &p) == 0);
    Lattice L;
    CHECK(lattice_chain_bc(&L, p.Lx, p.thop, p.bc_x) == 0);
    const int ph = model_ph_symmetric(&L, params_mu_is_half(&p));
    CHECK(L.is_bipartite == 0 && ph == 0);
    CHECK(params_extended_schema(&p, ph) == 1);
    lattice_free(&L);
}

/* replica_chain_init builds the model with the resolved mu (review R1). */
static void check_chain_model(void)
{
    Params p;
    Lattice L;
    CHECK(read_with(chain4, "mu=0.5\n", &p) == 0);
    lattice_chain(&L, 4, -1.0, 1);
    ReplicaChain c;
    CHECK(replica_chain_init(&c, &p, &L, 0, 10, p.dtau, 0, 3ULL, NULL) == 0);
    CHECK(c.m.mu == 0.5 && c.m.half_filling == 0 && c.m.ph_symmetric == 0);
    CHECK(c.D.use_ph == 0);
    /* K carries -mu on its diagonal */
    CHECK(c.m.K[0] == -0.5 && c.m.K[1 + 1 * 4] == -0.5);
    replica_chain_free(&c);

    CHECK(read_with(chain4, "", &p) == 0);
    CHECK(replica_chain_init(&c, &p, &L, 0, 10, p.dtau, 0, 3ULL, NULL) == 0);
    CHECK(c.m.mu == 2.0 && c.m.half_filling == 1 && c.m.ph_symmetric == 1);
    CHECK(c.D.use_ph == 1);
    replica_chain_free(&c);

    CHECK(read_with(chain4, "mu=2\n", &p) == 0);
    CHECK(replica_chain_init(&c, &p, &L, 0, 10, p.dtau, 0, 3ULL, NULL) == 0);
    CHECK(c.m.half_filling == 1 && c.D.use_ph == 1);
    replica_chain_free(&c);
    lattice_free(&L);
}

int main(void)
{
    check_mu();
    check_tprime();
    check_sign_file();
    check_odd_chain();
    check_chain_model();
    TEST_END();
}
